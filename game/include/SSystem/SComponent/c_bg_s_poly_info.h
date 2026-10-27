#ifndef C_BG_S_POLY_INFO_H
#define C_BG_S_POLY_INFO_H

#include "dolphin/types.h"
#include "JSystem/JUtility/JUTAssert.h"
#include "f_pc/f_pc_manager.h"
#include "global.h"

// How many collision bodies (cBgW) the game holds at once; this number is also "none" for a body's id and a poly's
// bg index. Randomizer+'s generated squares load several at a time, each with its land and its solid props
// (palms, big trees, chests, boulders), past the GameCube's 256.
#if TARGET_PC
#define BG_ID_MAX 1024
#else
#define BG_ID_MAX 256
#endif

class cBgW;

class cBgS_PolyInfo {
private:
    /* 0x00 */ u16 mPolyIndex;
    /* 0x02 */ u16 mBgIndex;
    /* 0x04 */ cBgW* mpBgW;
    /* 0x08 */ fpc_ProcID mActorId;

public:
    cBgS_PolyInfo() {
        ClearPi();
    }
    void ClearPi() {
        mPolyIndex = -1;
        mBgIndex = BG_ID_MAX;
        mpBgW = NULL;
        mActorId = fpcM_ERROR_PROCESS_ID_e;
    }
    void SetPolyInfo(const cBgS_PolyInfo& other) {
        *this = other;
    }
    void SetActorInfo(int bg_index, void* bgw, fpc_ProcID actor_id) {
        JUT_ASSERT(0x59, 0 <= bg_index);
        mBgIndex = bg_index;
        mpBgW = (cBgW*)bgw;
        mActorId = actor_id;
    }
    bool ChkSafe(const void* bgw, fpc_ProcID pid) const {
        if (mpBgW == bgw && mActorId == pid)
            return true;
        return false;
    }

    virtual ~cBgS_PolyInfo() {}

    int GetPolyIndex() const { return mPolyIndex; }
    int GetBgIndex() const { return mBgIndex; }
    bool ChkSetInfo() const {
        if (mPolyIndex == 0xFFFF || mBgIndex == BG_ID_MAX) {
            return false;
        }

        return true;
    }
    bool ChkSetInf() const {
        if (mPolyIndex == 0xFFFF || mBgIndex == BG_ID_MAX) {
            return false;
        }

        return true;
    }
    bool ChkBgIndex() const {
        if (mBgIndex == BG_ID_MAX) {
            return false;
        }
        return true;
    }
    void SetPolyIndex(int poly_index) {
        JUT_ASSERT(0x7b, 0 <= poly_index);
        mPolyIndex = poly_index;
    }
};  // Size: 0x10

STATIC_ASSERT(sizeof(cBgS_PolyInfo) == 0x10);

#endif /* C_BG_S_POLY_INFO_H */
