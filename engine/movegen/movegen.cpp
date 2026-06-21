#include "movegen.h"
#include "attacks.h"

namespace MoveGen {

static void add_pawn_promotions(MoveList& list, int from, int to, bool capture) {
    if (capture) {
        list.add(make_move(from, to, FLAG_PROMO_Q_CAP));
        list.add(make_move(from, to, FLAG_PROMO_R_CAP));
        list.add(make_move(from, to, FLAG_PROMO_B_CAP));
        list.add(make_move(from, to, FLAG_PROMO_N_CAP));
    } else {
        list.add(make_move(from, to, FLAG_PROMO_Q));
        list.add(make_move(from, to, FLAG_PROMO_R));
        list.add(make_move(from, to, FLAG_PROMO_B));
        list.add(make_move(from, to, FLAG_PROMO_N));
    }
}

void generate_pseudo(const Position& pos, MoveList& list) {
    Color us = pos.side, them = ~us;
    U64 own = pos.byColor[us];
    U64 enemy = pos.byColor[them];
    U64 occ = own | enemy;
    U64 empty = ~occ;

    // ---------------- Pawns ----------------
    U64 pawns = pos.pieces(us, PAWN);
    int up = (us == WHITE) ? 8 : -8;
    U64 promoRank = (us == WHITE) ? RANK_7_BB : RANK_2_BB;
    U64 startRank = (us == WHITE) ? RANK_2_BB : RANK_7_BB;

    U64 p = pawns;
    while (p) {
        int s = pop_lsb(p);
        bool onPromo = sq_bb(s) & promoRank;
        int t = s + up;

        // single (and double) push
        if (empty & sq_bb(t)) {
            if (onPromo) add_pawn_promotions(list, s, t, false);
            else {
                list.add(make_move(s, t, FLAG_QUIET));
                if (sq_bb(s) & startRank) {
                    int t2 = t + up;
                    if (empty & sq_bb(t2))
                        list.add(make_move(s, t2, FLAG_DOUBLE_PUSH));
                }
            }
        }
        // captures
        U64 atk = Attacks::pawn[us][s] & enemy;
        while (atk) {
            int c = pop_lsb(atk);
            if (onPromo) add_pawn_promotions(list, s, c, true);
            else list.add(make_move(s, c, FLAG_CAPTURE));
        }
        // en passant
        if (pos.epSquare != -1 && (Attacks::pawn[us][s] & sq_bb(pos.epSquare)))
            list.add(make_move(s, pos.epSquare, FLAG_EP_CAPTURE));
    }

    // ---------------- Knights ----------------
    U64 kn = pos.pieces(us, KNIGHT);
    while (kn) {
        int s = pop_lsb(kn);
        U64 a = Attacks::knight[s] & ~own;
        while (a) {
            int t = pop_lsb(a);
            list.add(make_move(s, t, (enemy & sq_bb(t)) ? FLAG_CAPTURE : FLAG_QUIET));
        }
    }

    // ---------------- Bishops ----------------
    U64 bi = pos.pieces(us, BISHOP);
    while (bi) {
        int s = pop_lsb(bi);
        U64 a = Attacks::bishop(s, occ) & ~own;
        while (a) {
            int t = pop_lsb(a);
            list.add(make_move(s, t, (enemy & sq_bb(t)) ? FLAG_CAPTURE : FLAG_QUIET));
        }
    }

    // ---------------- Rooks ----------------
    U64 rk = pos.pieces(us, ROOK);
    while (rk) {
        int s = pop_lsb(rk);
        U64 a = Attacks::rook(s, occ) & ~own;
        while (a) {
            int t = pop_lsb(a);
            list.add(make_move(s, t, (enemy & sq_bb(t)) ? FLAG_CAPTURE : FLAG_QUIET));
        }
    }

    // ---------------- Queens ----------------
    U64 qn = pos.pieces(us, QUEEN);
    while (qn) {
        int s = pop_lsb(qn);
        U64 a = Attacks::queen(s, occ) & ~own;
        while (a) {
            int t = pop_lsb(a);
            list.add(make_move(s, t, (enemy & sq_bb(t)) ? FLAG_CAPTURE : FLAG_QUIET));
        }
    }

    // ---------------- King (non-castling) ----------------
    int ks = pos.king_square(us);
    U64 ka = Attacks::king[ks] & ~own;
    while (ka) {
        int t = pop_lsb(ka);
        list.add(make_move(ks, t, (enemy & sq_bb(t)) ? FLAG_CAPTURE : FLAG_QUIET));
    }

    // ---------------- Castling ----------------
    // Rights imply the rook is home (rights are cleared when a rook leaves/dies).
    // Require: path squares empty, and king's start/through/target not attacked.
    if (us == WHITE) {
        if ((pos.castling & WHITE_OO)
            && (empty & sq_bb(F1)) && (empty & sq_bb(G1))
            && !pos.attacked_by(E1, them) && !pos.attacked_by(F1, them)
            && !pos.attacked_by(G1, them))
            list.add(make_move(E1, G1, FLAG_CASTLE_KING));
        if ((pos.castling & WHITE_OOO)
            && (empty & sq_bb(B1)) && (empty & sq_bb(C1)) && (empty & sq_bb(D1))
            && !pos.attacked_by(E1, them) && !pos.attacked_by(D1, them)
            && !pos.attacked_by(C1, them))
            list.add(make_move(E1, C1, FLAG_CASTLE_QUEEN));
    } else {
        if ((pos.castling & BLACK_OO)
            && (empty & sq_bb(F8)) && (empty & sq_bb(G8))
            && !pos.attacked_by(E8, them) && !pos.attacked_by(F8, them)
            && !pos.attacked_by(G8, them))
            list.add(make_move(E8, G8, FLAG_CASTLE_KING));
        if ((pos.castling & BLACK_OOO)
            && (empty & sq_bb(B8)) && (empty & sq_bb(C8)) && (empty & sq_bb(D8))
            && !pos.attacked_by(E8, them) && !pos.attacked_by(D8, them)
            && !pos.attacked_by(C8, them))
            list.add(make_move(E8, C8, FLAG_CASTLE_QUEEN));
    }
}

void generate_legal(Position& pos, MoveList& list) {
    MoveList pseudo;
    generate_pseudo(pos, pseudo);
    Color us = pos.side;
    for (int i = 0; i < pseudo.count; ++i) {
        Move m = pseudo.moves[i];
        pos.make(m);
        // after make, side flipped; legal iff our king is not attacked by them
        if (!pos.attacked_by(pos.king_square(us), ~us))
            list.add(m);
        pos.unmake(m);
    }
}

} // namespace MoveGen
