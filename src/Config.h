#pragma once

// Configuration mirror of the original BR180.ini.
// All keys were extracted verbatim from the original DLL's string table.
struct Config
{
    // [Rotation]
    bool  bReverseInputRot{ false };
    bool  bReverseMatrixMult{ true };
    float fUpdateRotX{ 0.0f };
    float fUpdateRotY{ 0.0f };
    float fUpdateRotZ{ 180.0f };

    float fTransitionEnterRotX{ 0.0f };
    float fTransitionEnterRotY{ 0.0f };
    float fTransitionEnterRotZ{ 180.0f };

    float fTransitionLeaveRotX{ 0.0f };
    float fTransitionLeaveRotY{ 0.0f };
    float fTransitionLeaveRotZ{ 0.0f };

    float fTransitionMoveRotX{ 0.0f };
    float fTransitionMoveRotY{ 0.0f };
    float fTransitionMoveRotZ{ 180.0f };

    // [Override]
    float fInitialPitch{ 310.0f };
    std::int16_t iExtendCellNW_Y{ 10 };
    std::int16_t iExtendCellSE_X{ 10 };
    // Rebuild-only switch (not in the original ini): rotate the worldspace's
    // northRotation by 180 degrees. Zero-risk struct write, may flip the
    // compass/map orientation. Set bFlipNorthRotation=0 in the ini to disable.
    bool bFlipNorthRotation{ true };
    // Rebuild-only: which transform to apply to the map render root.
    //   0 = off
    //   1 = mirror left/right  (flip X)
    //   2 = mirror top/bottom  (flip Y)
    //   3 = rotate 180 degrees
    // The original plugin's ini only ever expressed a Z rotation (fUpdateRot 0,0,180),
    // which is NOT the same as a mirror. Set this in [Override] to pick the effect.
    int iFlipMode{ 1 };
    // v15: Scaleform mirror position fine-tuning. The world-map clip's
    // registration point can differ, so after the automatic +width centering
    // these are added to X/Y. Only needed if the mirrored map is off-center.
    float fMirrorShiftX{ 0.0f };
    float fMirrorShiftY{ 0.0f };

    // [Hacks]
    bool bChangeXRotSignInEnterTransition{ true };
    bool bReverseCopyRotationInLeaveTransition{ true };

    static Config& GetSingleton();
    void           Load();
    void           Log() const;

private:
    Config() = default;
};
