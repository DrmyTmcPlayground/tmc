#include "midi.h"
#include "reader.h"
#include "util.h"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <fmt/format.h>
#include <util/file.h>

extern std::string gBaseromPath;

// Opcodes used for parsing out the start, end, and jumps in the track
// See https://www.romhacking.net/documents/462/ and https://loveemu.github.io/vgmdocs/Summary_of_GBA_Standard_Sound_Driver_MusicPlayer2000.html
constexpr int cmdWaitBegin = 0x80; // 0x80 to 0xB0 just means wait
constexpr int cmdWaitEnd = 0xB0; 
constexpr int cmdFine = 0xB1; // track end
constexpr int cmdGoto = 0xB2; // jump, 4-byte ROM address operand
constexpr int cmdPatt = 0xB3; // call pattern, 4-byte ROM address operand
constexpr int cmdPattEnd = 0xB4; 
constexpr int cmdTempo = 0xBB;
// Opcodes used for parsing reasons
constexpr int cmdTranspose = 0xBC;
constexpr int cmdOneParamRangeBegin = 0xBD; // 0xBD to 0xC5 all have 1 parameter
constexpr int cmdOneParamRangeEnd = 0xC5;
constexpr int cmdTune = 0xC8;
constexpr int cmdEcho = 0xCD;
constexpr int cmdTieEnd = 0xCE;
constexpr int cmdTieBegin = 0xCF;
constexpr int cmdNoteBegin = 0xD0;

int voiceGroupNumber(const nlohmann::json& options, const std::string& label) {
    const nlohmann::json* value = options.contains("group") ? &options["group"] : &options["G"];
    return value->is_number_integer() ? value->get<int>() : std::stoi(value->get<std::string>());
}

int skipOptionalArgs(const unsigned char* blob, std::uint32_t end, std::uint32_t from, int count) {
    std::uint32_t skip = from;
    for (int i = 0; i < count && skip < end && blob[skip] < cmdWaitBegin; ++i) {
        ++skip;
    }
        
    return skip;
}

void parseTrack(const unsigned char* blob, std::uint32_t start, std::uint32_t end, const std::string& name, std::vector<std::uint32_t>& sites) {    
    int lastCommand = -1;
    std::uint32_t parseIndex = start;
    while (parseIndex < end) {
        // We need to parse to find each of the goto/patt statements so we can adjust the pointers
        int command = blob[parseIndex];        
        std::uint32_t commandPosition = parseIndex;
        std::uint32_t parseArgIndex = parseIndex + 1;
        
        if (command < cmdWaitBegin) {
            command = lastCommand;
            parseArgIndex = parseIndex;
        } else if (command > cmdPattEnd && command != cmdTempo) {
            lastCommand = command;
        }
        
        if ((command >= cmdWaitBegin && command <= cmdWaitEnd) || command == cmdPattEnd) {
            parseIndex = parseArgIndex;
        } else if (command == cmdFine) {
            return;
        } else if (command == cmdGoto || command == cmdPatt) {
            // Record the location of the jump statement
            sites.push_back(parseArgIndex);
            parseIndex = parseArgIndex + 4;
        } else if (command == cmdTempo || command == cmdTranspose || command == cmdTune || (command >= cmdOneParamRangeBegin && command <= cmdOneParamRangeEnd)) {
            parseIndex = parseArgIndex + 1;
        } else if (command = cmdEcho) {
            parseIndex = parseArgIndex + 4;
        } else {
            switch (command) {
                case cmdTieEnd:
                    parseIndex = skipOptionalArgs(parseArgIndex, 1);
                    break;
                case cmdTieBegin:
                    parseIndex = skipOptionalArgs(parseArgIndex, 2);
                    break;
                default:
                    parseIndex = skipOptionalArgs(parseArgIndex, 3);
                    break;
            }
        }
    }
}

std::filesystem::path MidiAsset::generateAssetPath() {
    std::filesystem::path asset_path = path;
    return asset_path.replace_extension(".mid");
}

std::filesystem::path MidiAsset::generateBuildPath() {
    std::filesystem::path build_path = path;
    return build_path.replace_extension(".s");
}

void MidiAsset::extractBinary(const std::vector<char>& baserom) {
    // Custom extraction as we need a label in the middle.
    std::string label = path.stem();

    std::filesystem::path tracksPath = path;
    tracksPath.replace_filename(label + "_tracks.bin");

    int headerOffset = asset["options"]["headerOffset"];

    // Extract tracks
    {
        auto file = util::open_file(tracksPath, "w");
        std::fwrite(baserom.data() + start, 1, static_cast<size_t>(headerOffset), file.get());
    }
    
    // Extract header and goto/patt pointers
    // We have to extract the pointers to make the audio files properly shiftable
    const unsigned char* blob = reinterpret_cast<const unsigned char*>(baserom.data()) + start;
    std::uint32_t blobRomAddress = 0x08000000u + static_cast<std::uint32_t>(start);
    std::uint32_t blobSize = static_cast<std::uint32_t>(headerOffset);
    Reader reader(baserom, start, size);

    auto readU32 = [&reader](int offset) -> std::uint32_t {
        reader.cursor = offset;
        return reader.read_u32();
    };

    reader.cursor = headerOffset;

    // Read the blob header to find the track start positions
    unsigned numTracks = reader.read_u8();
    unsigned numBlocks = reader.read_u8();
    unsigned priority = reader.read_u8();
    unsigned reverb = reader.read_u8();
    std::string voiceGroupLabel = fmt::format("voicegroup{:03}", voiceGroupNumber(asset["options"], label));

    auto blobOffset = [](std::uint32_t romAddress) -> std::uint32_t {
        return romAddress - blobRomAddress;
    };

    std::vector<std::uint32_t> trackStartPositions;
    std::vector<std::uint32_t> sites;
    for (int i = 0; i < numTracks; ++i) {
        trackStartPositions.push_back(blobOffset(readU32(headerOffset + 8 + 4 * i)));
    }

    for (int i = 0; i < numTracks; ++i) {
        parseTrack(blob, trackStartPositions[i], i + 1 < numTracks ? trackStartPositions[i + 1] : blobSize, sites);
    }

    // Create the .s file, populating the pointer values for goto/patt statements to be used by the linker
    // This fixes the issue of ROM changes causing audio to be silent or completely non-functional
    auto file = util::open_file(buildPath, "w");
    fmt::print(file.get(), "{}_tracks:\n", label);
    std::uint32_t position = 0;
    for (std::uint32_t site : sites) {
        std::uint32_t pointerTarget = blobOffset(readU32(static_cast<int>(site)));
        fmt::print(file.get(), "\t.4byte {}_tracks + {:#x}\n", label, target);
        position = site + 4;
    }
    
    if (position < blobSize) {
        fmt::print(file.get(), "\t.incbin \"{}\", {}, {}\n", tracksPath.native(), position, blobSize - position);
    }
    
    fmt::print(file.get(), "{}::\n", label);
    fmt::print(file.get(), "\t.byte {}, {}, {}, {}\n", numTracks, numBlocks, priority, reverb);
    fmt::print(file.get(), "\t.4byte {}\n", voiceGroupLabel);
    for (std::uint32_t trackStartPosition : trackStartPositions) {
        fmt::print(file.get(), "\t.4byte {}_tracks + {:#x}\n", label, trackStartPosition);
    }
}

void MidiAsset::parseOptions(std::vector<std::string>& commonParams, std::vector<std::string>& agb2midParams) {
    bool exactGateTime = true;

    for (const auto& it : asset["options"].items()) {
        const std::string& key = it.key();
        if (key == "group" || key == "G") {
            commonParams.emplace_back("-G");
            commonParams.push_back(to_string(it.value()));
        } else if (key == "priority" || key == "P") {
            commonParams.emplace_back("-P");
            commonParams.push_back(to_string(it.value()));
        } else if (key == "reverb" || key == "R") {
            commonParams.emplace_back("-R");
            commonParams.push_back(to_string(it.value()));
        } else if (key == "nominator") {
            agb2midParams.emplace_back("-n");
            agb2midParams.push_back(to_string(it.value()));
        } else if (key == "denominator") {
            agb2midParams.emplace_back("-d");
            agb2midParams.push_back(to_string(it.value()));
        } else if (key == "timeChanges") {
            const nlohmann::json& value = it.value();
            if (value.is_array()) {
                // Multiple time changes
                for (const auto& change : value) {
                    agb2midParams.emplace_back("-t");
                    agb2midParams.push_back(to_string(change["nominator"]));
                    agb2midParams.push_back(to_string(change["denominator"]));
                    agb2midParams.push_back(to_string(change["time"]));
                }
            } else {
                agb2midParams.emplace_back("-t");
                agb2midParams.push_back(to_string(value["nominator"]));
                agb2midParams.push_back(to_string(value["denominator"]));
                agb2midParams.push_back(to_string(value["time"]));
            }
        } else if (key == "exactGateTime") {
            if (it.value().get<int>() == 1) {
                exactGateTime = true;
            } else {
                exactGateTime = false;
            }
        } else if (key == "headerOffset") {
            // ignore here
        } else {
            commonParams.push_back("-" + key);
            commonParams.push_back(to_string(it.value()));
        }
    }

    if (exactGateTime) {
        commonParams.emplace_back("-E");
    }
}

void MidiAsset::convertToHumanReadable(const std::vector<char>& baserom) {
    (void)baserom;

    // Convert the options
    std::vector<std::string> commonParams;
    std::vector<std::string> agb2midParams;
    parseOptions(commonParams, agb2midParams);

    int headerOffset = asset["options"]["headerOffset"];

    std::filesystem::path toolPath = "tools";
    std::vector<std::string> cmd;
    cmd.push_back(toolPath / "bin" / "agb2mid");
    cmd.push_back(gBaseromPath);
    cmd.push_back(fmt::format("{:#x}", start + headerOffset));
    cmd.push_back(gBaseromPath); // TODO deduplicate?
    cmd.push_back(assetPath);
    cmd.insert(cmd.end(), commonParams.begin(), commonParams.end());
    cmd.insert(cmd.end(), agb2midParams.begin(), agb2midParams.end());
    check_call(cmd);

    // We also need to build the mid to an s file here, so we get shiftability after converting.
    cmd.clear();
    cmd.push_back(toolPath / "bin" / "mid2agb");
    cmd.push_back(assetPath);
    cmd.push_back(buildPath);
    cmd.insert(cmd.end(), commonParams.begin(), commonParams.end());
    check_call(cmd);
}

void MidiAsset::buildToBinary() {
    // Convert the options
    std::vector<std::string> commonParams;
    std::vector<std::string> agb2midParams;
    parseOptions(commonParams, agb2midParams);
    std::filesystem::path toolPath = "tools";
    std::vector<std::string> cmd;
    cmd.push_back(toolPath / "bin" / "mid2agb");
    cmd.push_back(assetPath);
    cmd.push_back(buildPath);
    cmd.insert(cmd.end(), commonParams.begin(), commonParams.end());
    check_call(cmd);
}