#include "PCH.h"
#include "Config.h"

Config& Config::GetSingleton()
{
    static Config singleton;
    return singleton;
}

// Minimal INI reader that matches the original plugin's GetPrivateProfile* semantics.
// The original reads "Data\\SKSE\\Plugins\\br180.ini" relative to the game directory.
namespace
{
    std::string ReadValue(const std::string& a_path, const char* a_section, const char* a_key)
    {
        char  buf[256]{};
        auto  len = ::GetPrivateProfileStringA(a_section, a_key, "", buf, sizeof(buf), a_path.c_str());
        return std::string(buf, len);
    }

    bool ReadBool(const std::string& a_path, const char* a_section, const char* a_key, bool a_default)
    {
        auto v = ReadValue(a_path, a_section, a_key);
        if (v.empty()) {
            return a_default;
        }
        // Accept "true"/"false" and "1"/"0" like the original.
        if (v == "true" || v == "1" || v == "True" || v == "TRUE") {
            return true;
        }
        if (v == "false" || v == "0" || v == "False" || v == "FALSE") {
            return false;
        }
        return a_default;
    }

    float ReadFloat(const std::string& a_path, const char* a_section, const char* a_key, float a_default)
    {
        auto v = ReadValue(a_path, a_section, a_key);
        if (v.empty()) {
            return a_default;
        }
        try {
            return std::stof(v);
        } catch (...) {
            SKSE::log::warn("Failed to read value {}:{} from ini. Default value will be used.", a_section, a_key);
            return a_default;
        }
    }

    std::int16_t ReadI16(const std::string& a_path, const char* a_section, const char* a_key, std::int16_t a_default)
    {
        auto v = ReadValue(a_path, a_section, a_key);
        if (v.empty()) {
            return a_default;
        }
        try {
            auto n = std::stoi(v);
            if (n >= 0 && n <= 32767) {
                return static_cast<std::int16_t>(n);
            }
            return a_default;
        } catch (...) {
            return a_default;
        }
    }
}

void Config::Load()
{
    std::string path = "Data\\SKSE\\Plugins\\br180.ini";

    // ConfigA defaults (the active ini): input rot not reversed, matrix mult reversed.
    bReverseInputRot = ReadBool(path, "Rotation", "bReverseInputRot", bReverseInputRot);
    bReverseMatrixMult = ReadBool(path, "Rotation", "bReverseMatrixMult", bReverseMatrixMult);

    fUpdateRotX = ReadFloat(path, "Rotation", "fUpdateRotX", fUpdateRotX);
    fUpdateRotY = ReadFloat(path, "Rotation", "fUpdateRotY", fUpdateRotY);
    fUpdateRotZ = ReadFloat(path, "Rotation", "fUpdateRotZ", fUpdateRotZ);

    fTransitionEnterRotX = ReadFloat(path, "Rotation", "fTransitionEnterRotX", fTransitionEnterRotX);
    fTransitionEnterRotY = ReadFloat(path, "Rotation", "fTransitionEnterRotY", fTransitionEnterRotY);
    fTransitionEnterRotZ = ReadFloat(path, "Rotation", "fTransitionEnterRotZ", fTransitionEnterRotZ);

    fTransitionLeaveRotX = ReadFloat(path, "Rotation", "fTransitionLeaveRotX", fTransitionLeaveRotX);
    fTransitionLeaveRotY = ReadFloat(path, "Rotation", "fTransitionLeaveRotY", fTransitionLeaveRotY);
    fTransitionLeaveRotZ = ReadFloat(path, "Rotation", "fTransitionLeaveRotZ", fTransitionLeaveRotZ);

    fTransitionMoveRotX = ReadFloat(path, "Rotation", "fTransitionMoveRotX", fTransitionMoveRotX);
    fTransitionMoveRotY = ReadFloat(path, "Rotation", "fTransitionMoveRotY", fTransitionMoveRotY);
    fTransitionMoveRotZ = ReadFloat(path, "Rotation", "fTransitionMoveRotZ", fTransitionMoveRotZ);

    fInitialPitch = ReadFloat(path, "Override", "fInitialPitch", fInitialPitch);
    iExtendCellNW_Y = ReadI16(path, "Override", "iExtendCellNW_Y", iExtendCellNW_Y);
    iExtendCellSE_X = ReadI16(path, "Override", "iExtendCellSE_X", iExtendCellSE_X);
    bFlipNorthRotation = ReadBool(path, "Override", "bFlipNorthRotation", bFlipNorthRotation);
    bFlipMapCamera = ReadBool(path, "Override", "bFlipMapCamera", bFlipMapCamera);

    bChangeXRotSignInEnterTransition = ReadBool(path, "Hacks", "bChangeXRotSignInEnterTransition", bChangeXRotSignInEnterTransition);
    bReverseCopyRotationInLeaveTransition = ReadBool(path, "Hacks", "bReverseCopyRotationInLeaveTransition", bReverseCopyRotationInLeaveTransition);
}

void Config::Log() const
{
    SKSE::log::info("-----Started with this configuration-----");
    SKSE::log::info("bReverseInputRot={}", bReverseInputRot);
    SKSE::log::info("bReverseMatrixMult={}", bReverseMatrixMult);
    SKSE::log::info("fUpdateRot={},{},{}", fUpdateRotX, fUpdateRotY, fUpdateRotZ);
    SKSE::log::info("fTransitionEnterRot={},{},{}", fTransitionEnterRotX, fTransitionEnterRotY, fTransitionEnterRotZ);
    SKSE::log::info("fTransitionLeaveRot={},{},{}", fTransitionLeaveRotX, fTransitionLeaveRotY, fTransitionLeaveRotZ);
    SKSE::log::info("fTransitionMoveRot={},{},{}", fTransitionMoveRotX, fTransitionMoveRotY, fTransitionMoveRotZ);
    SKSE::log::info("fInitialPitch={}", fInitialPitch);
    SKSE::log::info("iExtendCellNW_Y={}", iExtendCellNW_Y);
    SKSE::log::info("iExtendCellSE_X={}", iExtendCellSE_X);
    SKSE::log::info("bFlipNorthRotation={}", bFlipNorthRotation);
    SKSE::log::info("bFlipMapCamera={}", bFlipMapCamera);
    SKSE::log::info("bChangeXRotSignInEnterTransition={}", bChangeXRotSignInEnterTransition);
    SKSE::log::info("bReverseCopyRotationInLeaveTransition={}", bReverseCopyRotationInLeaveTransition);
    SKSE::log::info("-----------------------------------------");
}
