#include "position.h"
#include "zobrist.h"
#include "../movegen/attacks.h"

// ----- low-level piece editing (keep bitboards, mailbox, and key in sync) -----
void Position::put_piece(Color c, PieceType pt, int sq) {
    U64 b = sq_bb(sq);
    byType[pt] |= b;
    byColor[c] |= b;
    board[sq] = pt;
    key ^= Zobrist::psq[c][pt][sq];
}

void Position::remove_piece(int sq) {
    PieceType pt = (PieceType)board[sq];
    Color c = color_on(sq);
    U64 b = sq_bb(sq);
    byType[pt] ^= b;
    byColor[c] ^= b;
    board[sq] = NO_PIECE_TYPE;
    key ^= Zobrist::psq[c][pt][sq];
}

void Position::move_piece(int from, int to) {
    PieceType pt = (PieceType)board[from];
    Color c = color_on(from);
    U64 ft = sq_bb(from) | sq_bb(to);
    byType[pt] ^= ft;
    byColor[c] ^= ft;
    board[from] = NO_PIECE_TYPE;
    board[to] = pt;
    key ^= Zobrist::psq[c][pt][from] ^ Zobrist::psq[c][pt][to];
}

// ----- setup -----
void Position::clear() {
    for (int i = 0; i < PIECE_TYPE_NB; ++i) byType[i] = 0;
    byColor[WHITE] = byColor[BLACK] = 0;
    for (int i = 0; i < SQUARE_NB; ++i) board[i] = NO_PIECE_TYPE;
    side = WHITE;
    castling = 0;
    epSquare = -1;
    halfmoveClock = 0;
    fullmoveNumber = 1;
    key = 0;
    ply = 0;
    histLen = 0;
}

void Position::set_startpos() {
    set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 56);
}

static bool char_to_piece(char ch, Color& c, PieceType& pt) {
    switch (ch) {
        case 'P': c = WHITE; pt = PAWN;   return true;
        case 'N': c = WHITE; pt = KNIGHT; return true;
        case 'B': c = WHITE; pt = BISHOP; return true;
        case 'R': c = WHITE; pt = ROOK;   return true;
        case 'Q': c = WHITE; pt = QUEEN;  return true;
        case 'K': c = WHITE; pt = KING;   return true;
        case 'p': c = BLACK; pt = PAWN;   return true;
        case 'n': c = BLACK; pt = KNIGHT; return true;
        case 'b': c = BLACK; pt = BISHOP; return true;
        case 'r': c = BLACK; pt = ROOK;   return true;
        case 'q': c = BLACK; pt = QUEEN;  return true;
        case 'k': c = BLACK; pt = KING;   return true;
    }
    return false;
}

bool Position::set_fen(const char* fen, int len) {
    clear();
    int i = 0;
    auto at_end = [&]() { return i >= len || fen[i] == '\0'; };

    // 1) piece placement, rank 8 -> rank 1
    int rank = 7, file = 0;
    for (; !at_end() && fen[i] != ' '; ++i) {
        char ch = fen[i];
        if (ch == '/') { rank--; file = 0; }
        else if (ch >= '1' && ch <= '8') { file += ch - '0'; }
        else {
            Color c; PieceType pt;
            if (char_to_piece(ch, c, pt) && rank >= 0 && file < 8)
                put_piece(c, pt, make_square(file, rank));
            file++;
        }
    }
    while (!at_end() && fen[i] == ' ') ++i;

    // 2) side to move
    if (!at_end()) { side = (fen[i] == 'b') ? BLACK : WHITE; ++i; }
    while (!at_end() && fen[i] == ' ') ++i;

    // 3) castling rights
    castling = 0;
    if (!at_end() && fen[i] == '-') { ++i; }
    else {
        for (; !at_end() && fen[i] != ' '; ++i) {
            switch (fen[i]) {
                case 'K': castling |= WHITE_OO;  break;
                case 'Q': castling |= WHITE_OOO; break;
                case 'k': castling |= BLACK_OO;  break;
                case 'q': castling |= BLACK_OOO; break;
            }
        }
    }
    while (!at_end() && fen[i] == ' ') ++i;

    // 4) en-passant square
    epSquare = -1;
    if (!at_end() && fen[i] != '-') {
        int f = fen[i] - 'a';
        int r = (i + 1 < len) ? fen[i + 1] - '1' : 0;
        if (f >= 0 && f < 8 && r >= 0 && r < 8) epSquare = make_square(f, r);
        i += 2;
    } else if (!at_end()) { ++i; }
    while (!at_end() && fen[i] == ' ') ++i;

    // 5) halfmove clock
    halfmoveClock = 0;
    while (!at_end() && fen[i] >= '0' && fen[i] <= '9') {
        halfmoveClock = halfmoveClock * 10 + (fen[i] - '0'); ++i;
    }
    while (!at_end() && fen[i] == ' ') ++i;

    // 6) fullmove number
    fullmoveNumber = 0;
    while (!at_end() && fen[i] >= '0' && fen[i] <= '9') {
        fullmoveNumber = fullmoveNumber * 10 + (fen[i] - '0'); ++i;
    }
    if (fullmoveNumber == 0) fullmoveNumber = 1;

    // fold side/castling/ep into the key
    if (side == BLACK) key ^= Zobrist::side;
    key ^= Zobrist::castling[castling];
    if (epSquare != -1) key ^= Zobrist::epFile[file_of(epSquare)];

    ply = 0;
    histLen = 1;
    history[0] = key;
    return true;
}

// ----- attack queries -----
bool Position::attacked_by(int sq, Color by) const {
    // pawns: squares attacking `sq` with a `by`-pawn == reverse-color pawn set on sq
    if (Attacks::pawn[~by][sq] & pieces(by, PAWN)) return true;
    if (Attacks::knight[sq] & pieces(by, KNIGHT)) return true;
    if (Attacks::king[sq] & pieces(by, KING)) return true;
    U64 occ = occupied();
    if (Attacks::bishop(sq, occ) & (pieces(by, BISHOP) | pieces(by, QUEEN))) return true;
    if (Attacks::rook(sq, occ) & (pieces(by, ROOK) | pieces(by, QUEEN))) return true;
    return false;
}

bool Position::in_check(Color c) const {
    return attacked_by(king_square(c), ~c);
}

bool Position::is_repetition() const {
    U64 cur = history[histLen - 1];
    int count = 0;
    for (int i = 0; i < histLen; ++i)
        if (history[i] == cur) ++count;
    return count >= 3;
}

// ----- make / unmake -----
void Position::make(Move m) {
    int from = move_from(m), to = move_to(m), flag = move_flag(m);
    Color us = side, them = ~side;
    PieceType pt = (PieceType)board[from];

    Undo& u = undoStack[ply];
    u.castling = castling;
    u.epSquare = epSquare;
    u.halfmove = halfmoveClock;
    u.key = key;
    u.captured = NO_PIECE_TYPE;

    if (epSquare != -1) key ^= Zobrist::epFile[file_of(epSquare)];
    key ^= Zobrist::castling[castling];

    halfmoveClock++;

    if (flag == FLAG_EP_CAPTURE) {
        int capSq = (us == WHITE) ? to - 8 : to + 8;
        u.captured = PAWN;
        remove_piece(capSq);
        halfmoveClock = 0;
    } else if (is_capture(m)) {
        u.captured = board[to];
        remove_piece(to);
        halfmoveClock = 0;
    }
    if (pt == PAWN) halfmoveClock = 0;

    move_piece(from, to);

    if (is_promotion(m)) {
        remove_piece(to);                  // remove the pawn that just arrived
        put_piece(us, promo_type(m), to);  // place the promoted piece
    }

    if (flag == FLAG_CASTLE_KING)
        move_piece(us == WHITE ? H1 : H8, us == WHITE ? F1 : F8);
    else if (flag == FLAG_CASTLE_QUEEN)
        move_piece(us == WHITE ? A1 : A8, us == WHITE ? D1 : D8);

    int newEp = -1;
    if (flag == FLAG_DOUBLE_PUSH) newEp = (us == WHITE) ? from + 8 : from - 8;

    // update castling rights (clearing applies whether or not a rook is really there)
    int cr = castling;
    if (pt == KING) cr &= (us == WHITE) ? ~(WHITE_OO | WHITE_OOO) : ~(BLACK_OO | BLACK_OOO);
    if (from == H1 || to == H1) cr &= ~WHITE_OO;
    if (from == A1 || to == A1) cr &= ~WHITE_OOO;
    if (from == H8 || to == H8) cr &= ~BLACK_OO;
    if (from == A8 || to == A8) cr &= ~BLACK_OOO;
    castling = cr;
    key ^= Zobrist::castling[castling];

    epSquare = newEp;
    if (epSquare != -1) key ^= Zobrist::epFile[file_of(epSquare)];

    side = them;
    key ^= Zobrist::side;
    if (us == BLACK) fullmoveNumber++;

    ply++;
    history[histLen++] = key;
}

void Position::unmake(Move m) {
    ply--;
    histLen--;
    Undo& u = undoStack[ply];
    int from = move_from(m), to = move_to(m), flag = move_flag(m);
    Color us = ~side;   // the side that made the move
    side = us;

    if (is_promotion(m)) {
        remove_piece(to);
        put_piece(us, PAWN, to);
    }

    move_piece(to, from);

    if (flag == FLAG_CASTLE_KING)
        move_piece(us == WHITE ? F1 : F8, us == WHITE ? H1 : H8);
    else if (flag == FLAG_CASTLE_QUEEN)
        move_piece(us == WHITE ? D1 : D8, us == WHITE ? A1 : A8);

    if (flag == FLAG_EP_CAPTURE) {
        int capSq = (us == WHITE) ? to - 8 : to + 8;
        put_piece(~us, PAWN, capSq);
    } else if (u.captured != NO_PIECE_TYPE) {
        put_piece(~us, (PieceType)u.captured, to);
    }

    castling = u.castling;
    epSquare = u.epSquare;
    halfmoveClock = u.halfmove;
    key = u.key;
    if (us == BLACK) fullmoveNumber--;
}
