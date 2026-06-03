#pragma once
#include <cstdint>

// ============================================================
//  WeaponIndex — weapon-ID to name + ASCII art icon lookup
//  Ported from WeaponIndex.cs (Nation_Internal / bastab cheats)
//  ASCII art: compact single-line weapon silhouettes for the
//  ESP overlay.  Each icon uses printable ASCII only so it
//  renders correctly with wglUseFontBitmapsA.
// ============================================================

namespace WeaponIndex
{
    // ── Name lookup ───────────────────────────────────────────────────────────
    // Returns the weapon name string for the given ID.
    // ID 0 = "AK47" bot variant (shown for bots).
    // Unknown/grenade/special IDs return nullptr → overlay skips rendering.
    inline const char* GetWeaponName(int id)
    {
        switch (id)
        {
        case   0: return "AK47";
        case   1: return "FIST";
        case   2: return "M4A1";
        case   3: return "USP";
        case   4: return "AWM";
        case   5: return "M1014";
        case   6: return "AK47";
        case   7: return "UMP";
        case   8: return "MP5";
        case   9: return "Desert-Eagle";
        case  10: return "G18";
        case  11: return "M14";
        case  12: return "SCAR";
        case  13: return "VSS";
        case  14: return "GROZA";
        case  15: return "MP40";
        case  16: return "PAN";
        case  17: return "PARANG";
        case  18: return "SKS";
        case  19: return "M249";
        case  20: return "M1873";
        case  21: return "KAR98K";
        case  24: return "FAMAS";
        case  25: return "M500";
        case  26: return "SVD";
        case  27: return "BAT";
        case  28: return "XM8";
        case  29: return "SPAS12";
        case  30: return "M60";
        case  32: return "P90";
        case  33: return "AN94";
        case  34: return "KATANA";
        case  35: return "CG15";
        case  39: return "PLASMA";
        case  41: return "M1887";
        case  43: return "THOMPSON";
        case  45: return "M82B";
        case  46: return "AUG";
        case  47: return "PARAFAL";
        case  48: return "WOODPECKER";
        case  49: return "VECTOR";
        case  50: return "MAG7";
        case  51: return "SCYTHE";
        case  54: return "KORD";
        case  55: return "M1917";
        case  56: return "USP2";
        case  57: return "KINGFISHER";
        case  58: return "MINI-UZI";
        case  60: return "MP5";
        case  61: return "M60";
        case  62: return "VSS";
        case  63: return "M14";
        case  64: return "KAR98K";
        case  65: return "AWM-Y";
        case  67: return "FAMAS";
        case  70: return "GROZA";
        case  71: return "M249";
        case  72: return "SVD";
        case  73: return "G36";
        case  74: return "G36";
        case  75: return "M24";
        case  78: return "HEALSNIPER";
        case  80: return "M4A1";
        case  81: return "M4A1";
        case  82: return "M4A1";
        case  86: return "CHARGE BUSTER";
        case  88: return "MAC10";
        case  89: return "AC80";
        case  93: return "HEAL-PISTOL";
        case  99: return "SHIELD-GUN";
        case 100: return "FLAMTHROWER";
        case 119: return "M1887";
        case 120: return "MP5";
        case 121: return "MP5";
        case 122: return "M60";
        case 123: return "M60";
        case 124: return "VSS";
        case 125: return "VSS";
        case 126: return "M14";
        case 127: return "M14";
        case 128: return "KAR98K";
        case 129: return "KAR98K";
        case 130: return "FAMAS";
        case 131: return "FAMAS";
        case 150: return "BIZON";
        case 178: return "SCAR";
        case 179: return "SCAR";
        case 180: return "SCAR";
        case 181: return "TROGON";
        case 184: return "M1014";
        case 185: return "M1014";
        case 186: return "M1014";
        case 193: return "AUG";
        case 194: return "AUG";
        case 195: return "AUG";
        case 197: return "VSK94";
        case 228: return "MAC10";
        case 229: return "MAC10";
        case 230: return "MAC10";
        case 21001: return "HEAL-PISTOL";
        case 21002: return "M590";
        default:  return nullptr;
        }
    }

    // ── ASCII art icon lookup ─────────────────────────────────────────────────
    // Returns a compact ~12-char ASCII weapon silhouette for the given ID.
    // Designed for wglUseFontBitmapsA (printable ASCII 32-126 only).
    // Format: [barrel][body/action][muzzle]  +  name  (combined in esp.cpp)
    //
    // Icon key:
    //   --[=|=]--> Assault rifle  (curved mag, long barrel)
    //   ----[>]--  Sniper / DMR   (very long barrel)
    //   -=[=]-->   SMG            (short barrel, boxy body)
    //   -{.}=>     Pistol         (compact)
    //   ={||}=>    Shotgun        (wide bore)
    //   =[===]=>   LMG            (heavy, box mag)
    //   -------|   Blade melee    (long edge)
    //   -----|/    Curved blade   (Parang / Scythe)
    //   ===(O)     Blunt melee    (Pan, bat-like)
    //   ====|      Bat / stick
    //   -{*}->     Energy/Special
    //   +{=}->     Heal weapon
    //   =[O]|>     Shield-Gun
    //   {-_-}      Fist / no weapon
    inline const char* GetWeaponAscii(int id)
    {
        switch (id)
        {
        // ── Assault rifles ────────────────────────────────────────────────────
        case   0: return "--[=|=]-->";   // AK47 (bot)
        case   2: return "--[=|=]-->";   // M4A1
        case   6: return "--[=|=]-->";   // AK47
        case  12: return "--[=|=]-->";   // SCAR
        case  14: return "--[=|=]-->";   // GROZA
        case  24: return "--[=|=]-->";   // FAMAS
        case  28: return "--[=|=]-->";   // XM8
        case  33: return "--[=|=]-->";   // AN94
        case  46: return "--[=|=]-->";   // AUG
        case  47: return "--[=|=]-->";   // PARAFAL
        case  48: return "--[=|=]-->";   // WOODPECKER
        case  67: return "--[=|=]-->";   // FAMAS-I
        case  70: return "--[=|=]-->";   // GROZA-X
        case  73: return "--[=|=]-->";   // G36
        case  74: return "--[=|=]-->";   // G36
        case  80: return "--[=|=]-->";   // M4A1-I
        case  81: return "--[=|=]-->";   // M4A1-II
        case  82: return "--[=|=]-->";   // M4A1-III
        case  89: return "--[=|=]-->";   // AC80
        case 130: return "--[=|=]-->";   // FAMAS-II
        case 131: return "--[=|=]-->";   // FAMAS-III
        case 178: return "--[=|=]-->";   // SCAR-I
        case 179: return "--[=|=]-->";   // SCAR-II
        case 180: return "--[=|=]-->";   // SCAR-III
        case 193: return "--[=|=]-->";   // AUG-I
        case 194: return "--[=|=]-->";   // AUG-II
        case 195: return "--[=|=]-->";   // AUG-III

        // ── Sniper rifles ─────────────────────────────────────────────────────
        case   4: return "--------[>]";  // AWM
        case  21: return "--------[>]";  // KAR98K
        case  26: return "--------[>]";  // SVD
        case  45: return "--------[>]";  // M82B
        case  65: return "--------[>]";  // AWM-Y
        case  72: return "--------[>]";  // SVD-Y
        case  75: return "--------[>]";  // M24
        case 128: return "--------[>]";  // KAR98K-II
        case 129: return "--------[>]";  // KAR98K-III
        case  64: return "--------[>]";  // KAR98K-I

        // ── Marksman / semi-auto rifles ───────────────────────────────────────
        case  11: return "-----[=|]->";  // M14
        case  13: return "-----[=|]->";  // VSS
        case  18: return "-----[=|]->";  // SKS
        case  62: return "-----[=|]->";  // VSS-I
        case  63: return "-----[=|]->";  // M14-I
        case  71: return "-----[=|]->";  // M249-X (use DMR style)
        case 124: return "-----[=|]->";  // VSS-II
        case 125: return "-----[=|]->";  // VSS-III
        case 126: return "-----[=|]->";  // M14-II
        case 127: return "-----[=|]->";  // M14-III
        case 197: return "-----[=|]->";  // VSK94

        // ── SMGs ──────────────────────────────────────────────────────────────
        case   7: return "-=[=|]-->";    // UMP
        case   8: return "-=[=|]-->";    // MP5
        case  15: return "-=[=|]-->";    // MP40
        case  32: return "-=[=|]-->";    // P90
        case  43: return "-=[=|]-->";    // THOMPSON
        case  49: return "-=[=|]-->";    // VECTOR
        case  58: return "-=[=|]-->";    // MINI-UZI
        case  60: return "-=[=|]-->";    // MP5-I
        case  88: return "-=[=|]-->";    // MAC10
        case 120: return "-=[=|]-->";    // MP5-II
        case 121: return "-=[=|]-->";    // MP5-III
        case 150: return "-=[=|]-->";    // BIZON
        case 228: return "-=[=|]-->";    // MAC10-I
        case 229: return "-=[=|]-->";    // MAC10-II
        case 230: return "-=[=|]-->";    // MAC10-III

        // ── Pistols ───────────────────────────────────────────────────────────
        case   3: return "-{.}=>";       // USP
        case   9: return "-{.}=>";       // Desert-Eagle
        case  10: return "-{.}=>";       // G18
        case  55: return "-{.}=>";       // M1917
        case  56: return "-{.}=>";       // USP2
        case  57: return "-{.}=>";       // KINGFISHER

        // ── Shotguns ──────────────────────────────────────────────────────────
        case   5: return "={||}=>";      // M1014
        case  20: return "={||}=>";      // M1873
        case  25: return "={||}=>";      // M500
        case  29: return "={||}=>";      // SPAS12
        case  41: return "={||}=>";      // M1887
        case  50: return "={||}=>";      // MAG7
        case 119: return "={||}=>";      // M1887-X
        case 184: return "={||}=>";      // M1014-I
        case 185: return "={||}=>";      // M1014-II
        case 186: return "={||}=>";      // M1014-III
        case 21002: return "={||}=>";    // M590

        // ── LMGs ──────────────────────────────────────────────────────────────
        case  19: return "=[====]>";     // M249
        case  30: return "=[====]>";     // M60
        case  54: return "=[====]>";     // KORD
        case  61: return "=[====]>";     // M60-I
     //   case  71: return "=[====]>";     // M249-X
        case 122: return "=[====]>";     // M60-II
        case 123: return "=[====]>";     // M60-III

        // ── Melee — blades ────────────────────────────────────────────────────
        case  34: return "--------|";    // KATANA
        case  51: return "-----|/";      // SCYTHE  (curved)

        // ── Melee — curved ────────────────────────────────────────────────────
        case  17: return "-----|/";      // PARANG

        // ── Melee — blunt ─────────────────────────────────────────────────────
        case  16: return "===(O)";       // PAN
        case  27: return "====|";        // BAT

        // ── Special / Energy ──────────────────────────────────────────────────
        case  35: return "-{*}->";       // CG15
        case  39: return "-{*}->";       // PLASMA
        case  86: return "-{*}->";       // CHARGE BUSTER
        case 100: return "-{*}->";       // FLAMTHROWER
        case 181: return "-{*}->";       // TROGON

        // ── Heal weapons ──────────────────────────────────────────────────────
        case  78: return "+{=}->";       // HEALSNIPER
        case  93: return "+{=}->";       // HEAL-PISTOL
        case 21001: return "+{=}->";     // HEAL-PISTOL-Y

        // ── Shield-Gun ────────────────────────────────────────────────────────
        case  99: return "=[O]|>";       // SHIELD-GUN

        // ── Fist / no weapon ─────────────────────────────────────────────────
        case   1: return "{-_-}";        // FIST (bots)

        default:  return "--->";         // fallback generic
        }
    }
}
