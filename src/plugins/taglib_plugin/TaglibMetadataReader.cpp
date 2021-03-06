//////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2007-2017 musikcube team
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright notice,
//      this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the author nor the names of other contributors may
//      be used to endorse or promote products derived from this software
//      without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////////

#include "TaglibMetadataReader.h"

#ifdef WIN32
    #include <taglib/toolkit/tlist.h>
    #include <taglib/toolkit/tfile.h>
    #include <taglib/tag.h>
    #include <taglib/fileref.h>
    #include <taglib/audioproperties.h>
    #include <taglib/mpeg/mpegfile.h>
    #include <taglib/mpeg/id3v1/id3v1tag.h>
    #include <taglib/mpeg/id3v1/id3v1genres.h>
    #include <taglib/mpeg/id3v2/id3v2tag.h>
    #include <taglib/mpeg/id3v2/id3v2header.h>
    #include <taglib/mpeg/id3v2/id3v2frame.h>
    #include <taglib/mpeg/id3v2/frames/attachedpictureframe.h>
    #include <taglib/mpeg/id3v2/frames/commentsframe.h>
    #include <taglib/mpeg/id3v2/frames/textidentificationframe.h>
    #include <taglib/mp4/mp4file.h>
    #include <taglib/ogg/oggfile.h>
    #include <taglib/ogg/xiphcomment.h>
    #include <taglib/flac/flacfile.h>
    #include <taglib/toolkit/tpropertymap.h>
#else
    #include <taglib/tlist.h>
    #include <taglib/tfile.h>
    #include <taglib/tag.h>
    #include <taglib/fileref.h>
    #include <taglib/audioproperties.h>
    #include <taglib/mpegfile.h>
    #include <taglib/id3v1tag.h>
    #include <taglib/id3v1genres.h>
    #include <taglib/id3v2tag.h>
    #include <taglib/id3v2header.h>
    #include <taglib/id3v2frame.h>
    #include <taglib/attachedpictureframe.h>
    #include <taglib/commentsframe.h>
    #include <taglib/mp4file.h>
    #include <taglib/oggfile.h>
    #include <taglib/flacfile.h>
    #include <taglib/xiphcomment.h>
    #include <taglib/tpropertymap.h>
#include <taglib/textidentificationframe.h>
#endif

#include <vector>
#include <string>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <iostream>

using namespace musik::core::sdk;

#ifdef WIN32
static inline std::wstring utf8to16(const char* utf8) {
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, 0, 0);
    if (size <= 0) return L"";
    wchar_t* buffer = new wchar_t[size];
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, buffer, size);
    std::wstring utf16fn(buffer);
    delete[] buffer;
    return utf16fn;
}
#endif

static bool isValidYear(const std::string& year) {
    return std::stoi(year) > 0;
}

static float toReplayGainFloat(const std::string& input) {
    /* trim any trailing " db" or "db" noise... */
    std::string lower = boost::algorithm::to_lower_copy(input);
    if (lower.find(" db") == lower.length() - 3) {
        lower = lower.substr(0, lower.length() - 3);
    }
    else if (lower.find("db") == lower.length() - 2) {
        lower = lower.substr(0, lower.length() - 2);
    }

    try {
        return std::stof(lower);
    }
    catch (...) {
        /* nothing we can do... */
    }

    return 1.0f;
}

static inline void initReplayGain(ReplayGain& rg) {
    rg.albumGain = rg.albumPeak = 1.0f;
    rg.trackGain = rg.trackPeak = 1.0f;
}

static inline bool replayGainValid(ReplayGain& rg) {
    return rg.albumGain != 1.0 || rg.albumPeak != 1.0 ||
        rg.trackGain != 1.0 || rg.trackPeak != 1.0;
}

TaglibMetadataReader::TaglibMetadataReader() {
}

TaglibMetadataReader::~TaglibMetadataReader() {
}

void TaglibMetadataReader::Release() {
    delete this;
}

bool TaglibMetadataReader::CanRead(const char *extension){
    if (extension) {
        std::string ext(extension);
        boost::algorithm::to_lower(ext);

        return
            ext.compare("mp3") == 0 ||
            ext.compare("ogg") == 0 ||
            ext.compare("aac") == 0 ||
            ext.compare("m4a") == 0 ||
            ext.compare("flac") == 0 ||
            ext.compare("ape") == 0 ||
            ext.compare("mpc") == 0;
    }

    return false;
}

bool TaglibMetadataReader::Read(const char* uri, ITagStore *track) {
    std::string path(uri);
    std::string extension;

    std::string::size_type lastDot = path.find_last_of(".");
    if (lastDot != std::string::npos) {
        extension = path.substr(lastDot + 1).c_str();
    }

    bool success = false;

    if (extension.size()) {
        boost::algorithm::to_lower(extension);

        if (extension == "mp3") {
            try {
                success = this->ReadID3V2(uri, track);
            }
            catch (...) {
                std::cerr << "id3v2 tag read for " << uri << "failed!";
            }
        }
    }

    try {
        success |= this->ReadGeneric(uri, track);
    }
    catch (...) {
        std::cerr << "generic tag read for " << uri << "failed!";
    }

    return success;
}

bool TaglibMetadataReader::ReadGeneric(const char* uri, ITagStore *target) {
#ifdef WIN32
    TagLib::FileRef file(utf8to16(uri).c_str());
#else
    TagLib::FileRef file(uri);
#endif

    if (!file.isNull()) {
        TagLib::Tag *tag = file.tag();

        if (tag) {
            if (!tag->title().isEmpty()) {
                this->SetTagValue("title", tag->title(), target);
            }
            else {
                this->SetTagValue("title", uri, target);
            }

            this->SetTagValue("album",tag->album(), target);
            this->SetSlashSeparatedValues("artist",tag->artist() , target);
            this->SetTagValue("genre",tag->genre(), target);
            this->SetTagValue("comment",tag->comment(), target);

            if (tag->track()) {
                this->SetTagValue("track", tag->track(), target);
            }

            if (tag->year()) {
                this->SetTagValue("year", tag->year(), target);
            }

            /* read some generic key/value pairs that don't have direct accessors */
            this->ReadFromMap(tag->properties(), target);

            /* taglib hides certain properties (like album artist) in the XiphComment's
            field list. if we're dealing with a straight-up Xiph tag, process it now */
            auto xiphTag = dynamic_cast<TagLib::Ogg::XiphComment*>(tag);
            if (xiphTag) {
                this->ReadFromMap(xiphTag->fieldListMap(), target);
                this->ExtractReplayGain(xiphTag->fieldListMap(), target);
            }

            /* if this isn't a xiph tag, the file format may have some other custom
            properties. let's see if we can pull them out here... */
            if (!xiphTag) {
                bool handled = false;

                /* flac files may have more than one type of tag embedded. see if there's
                see if there's a xiph comment burried deep. */
                auto flacFile = dynamic_cast<TagLib::FLAC::File*>(file.file());
                if (flacFile && flacFile->hasXiphComment()) {
                    this->ReadFromMap(flacFile->xiphComment()->fieldListMap(), target);
                    this->ExtractReplayGain(flacFile->xiphComment()->fieldListMap(), target);
                    handled = true;
                }

                /* similarly, mp4 buries disc number and album artist. however, taglib does
                NOT exposed a map with normalized keys, so we have to do special property
                handling here... */
                if (!handled) {
                    auto mp4File = dynamic_cast<TagLib::MP4::File*>(file.file());
                    if (mp4File && mp4File->hasMP4Tag()) {
                        auto mp4TagMap = static_cast<TagLib::MP4::Tag*>(tag)->itemListMap();
                        this->ExtractValueForKey(mp4TagMap, "aART", "album_artist", target);
                        this->ExtractValueForKey(mp4TagMap, "disk", "disc", target);
                        this->ExtractReplayGain(mp4TagMap, target);
                    }
                }
            }

            TagLib::AudioProperties *audio = file.audioProperties();
            this->SetAudioProperties(audio, target);

            return true;
        }
    }

    return false;
}

void TaglibMetadataReader::ExtractValueForKey(
    const TagLib::MP4::ItemMap& map,
    const std::string& inputKey,
    const std::string& outputKey,
    ITagStore *target)
{
    if (map.contains(inputKey.c_str())) {
        TagLib::StringList value = map[inputKey.c_str()].toStringList();
        if (value.size()) {
            this->SetTagValue(outputKey.c_str(), value[0], target);
        }
    }
}

std::string TaglibMetadataReader::ExtractValueForKey(
    const TagLib::MP4::ItemMap& map,
    const std::string& inputKey,
    const std::string& defaultValue)
{
    if (map.contains(inputKey.c_str())) {
        TagLib::StringList value = map[inputKey.c_str()].toStringList();
        if (value.size()) {
            return value[0].to8Bit();
        }
    }
    return defaultValue;
}

template <typename T>
void TaglibMetadataReader::ExtractValueForKey(
    const T& map,
    const std::string& inputKey,
    const std::string& outputKey,
    ITagStore *target)
{
    if (map.contains(inputKey.c_str())) {
        TagLib::StringList value = map[inputKey.c_str()];
        if (value.size()) {
            this->SetTagValue(outputKey.c_str(), value[0], target);
        }
    }
}

template <typename T>
void TaglibMetadataReader::ReadFromMap(const T& map, ITagStore *target) {
    ExtractValueForKey(map, "DISCNUMBER", "disc", target);
    ExtractValueForKey(map, "ALBUM ARTIST", "album_artist", target);
    ExtractValueForKey(map, "ALBUMARTIST", "album_artist", target);
}

template <typename T>
std::string TaglibMetadataReader::ExtractValueForKey(
    const T& map,
    const std::string& inputKey,
    const std::string& defaultValue)
{
    if (map.contains(inputKey.c_str())) {
        TagLib::StringList value = map[inputKey.c_str()];
        if (value.size()) {
            return value[0].to8Bit();
        }
    }
    return defaultValue;
}

template <typename T>
void TaglibMetadataReader::ExtractReplayGain(const T& map, ITagStore *target)
{
    try {
        ReplayGain replayGain;
        initReplayGain(replayGain);
        replayGain.trackGain = toReplayGainFloat(ExtractValueForKey(map, "REPLAYGAIN_TRACK_GAIN", "1.0"));
        replayGain.trackPeak = toReplayGainFloat(ExtractValueForKey(map, "REPLAYGAIN_TRACK_PEAK", "1.0"));
        replayGain.albumGain = toReplayGainFloat(ExtractValueForKey(map, "REPLAYGAIN_ALBUM_GAIN", "1.0"));
        replayGain.albumPeak = toReplayGainFloat(ExtractValueForKey(map, "REPLAYGAIN_ALBUM_PEAK", "1.0"));

        if (replayGainValid(replayGain)) {
            target->SetReplayGain(replayGain);
        }
    }
    catch (...) {
        /* let's not allow weird replay gain tags to crash indexing... */
    }
}

bool TaglibMetadataReader::ReadID3V2(const char* uri, ITagStore *track) {
    TagLib::ID3v2::FrameFactory::instance()->setDefaultTextEncoding(TagLib::String::UTF8);

#ifdef WIN32
    TagLib::MPEG::File file(utf8to16(uri).c_str());
#else
    TagLib::MPEG::File file(uri);
#endif

    TagLib::ID3v2::Tag *id3v2 = file.ID3v2Tag();

    if (id3v2) {
        TagLib::AudioProperties *audio = file.audioProperties();
        TagLib::ID3v2::FrameListMap allTags = id3v2->frameListMap();

        if (!id3v2->title().isEmpty()) {
            this->SetTagValue("title", id3v2->title(), track);
        }

        this->SetTagValue("album", id3v2->album(), track);

        /* year */

        if (!track->Contains("year") && !allTags["TYER"].isEmpty()) { /* ID3v2.3*/
            auto year = allTags["TYER"].front()->toString().substr(0, 4);
            if (isValidYear(year.to8Bit())) {
                this->SetTagValue("year", year, track);
            }
        }

        if (!track->Contains("year") && !allTags["TDRC"].isEmpty()) { /* ID3v2.4*/
            auto year = allTags["TDRC"].front()->toString().substr(0, 4);
            if (isValidYear(year.to8Bit())) {
                this->SetTagValue("year", year, track);
            }
        }

        if (!track->Contains("year") && !allTags["TCOP"].isEmpty()) { /* ID3v2.3*/
            auto year = allTags["TCOP"].front()->toString().substr(0, 4);
            if (isValidYear(year.to8Bit())) {
                this->SetTagValue("year", year, track);
            }
        }

        /* replay gain */

        auto txxx = allTags["TXXX"];
        if (!txxx.isEmpty()) {
            ReplayGain replayGain;
            initReplayGain(replayGain);

            for (auto current : txxx) {
                using UTIF = TagLib::ID3v2::UserTextIdentificationFrame;
                UTIF* utif = dynamic_cast<UTIF*>(current);
                if (utif) {
                    auto name = utif->description().upper();
                    auto values = utif->fieldList();
                    if (values.size() > 0) {
                        if (name == "REPLAYGAIN_TRACK_GAIN") {
                            replayGain.trackGain = toReplayGainFloat(utif->fieldList().back().to8Bit());
                        }
                        else if (name == "REPLAYGAIN_TRACK_PEAK") {
                            replayGain.trackPeak = toReplayGainFloat(utif->fieldList().back().to8Bit());
                        }
                        else if (name == "REPLAYGAIN_ALBUM_GAIN") {
                            replayGain.albumGain = toReplayGainFloat(utif->fieldList().back().to8Bit());
                        }
                        else if (name == "REPLAYGAIN_ALBUM_PEAK") {
                            replayGain.albumPeak = toReplayGainFloat(utif->fieldList().back().to8Bit());
                        }
                    }
                }
            }

            if (replayGainValid(replayGain)) {
                track->SetReplayGain(replayGain);
            }
        }

        /* TRCK is the track number (or "trackNum/totalTracks") */

        std::vector<std::string> splitTrack;
        if (!allTags["TRCK"].isEmpty()) {
            std::string tempTrack = allTags["TRCK"].front()->toString().toCString(true);
            boost::algorithm::split(splitTrack, tempTrack, boost::algorithm::is_any_of("/"));
            this->SetTagValue("track", splitTrack[0].c_str(), track);

            if (splitTrack.size() > 1) {
                this->SetTagValue("totaltracks", splitTrack[1].c_str(), track);
            }
        }

        this->SetTagValues("bpm", allTags["TBPM"], track);
        this->SetSlashSeparatedValues("composer", allTags["TCOM"], track);
        this->SetTagValues("copyright", allTags["TCOP"], track);
        this->SetTagValues("encoder", allTags["TENC"], track);
        this->SetTagValues("writer", allTags["TEXT"], track);
        this->SetTagValues("org.writer", allTags["TOLY"], track);
        this->SetSlashSeparatedValues("publisher", allTags["TPUB"], track);
        this->SetTagValues("mood", allTags["TMOO"], track);
        this->SetSlashSeparatedValues("org.artist", allTags["TOPE"], track);
        this->SetTagValues("language", allTags["TLAN"], track);
        this->SetTagValues("disc", allTags["TPOS"], track);
        this->SetTagValues("lyrics", allTags["USLT"], track);
        this->SetTagValues("disc", allTags["TPOS"], track);

        /* genre. note that multiple genres may be present */

        if (!allTags["TCON"].isEmpty()) {
            TagLib::ID3v2::FrameList genres = allTags["TCON"];

            TagLib::ID3v2::FrameList::ConstIterator it = genres.begin();

            for (; it != genres.end(); ++it) {
                TagLib::String genreString = (*it)->toString();

                if (!genreString.isEmpty()) {
                    /* note1: apparently genres will already be de-duped */
                    int numberLength = 0;
                    bool isNumber = true;

                    TagLib::String::ConstIterator charIt = genreString.begin();
                    for (; isNumber && charIt != genreString.end(); ++charIt) {
                        isNumber = (*charIt >= '0' && *charIt <= '9');

                        if (isNumber) {
                            ++numberLength;
                        }
                    }

                    if (isNumber) { /* old ID3v1 tags had numbers for genres. */
                        int genreNumber = genreString.toInt();
                        if (genreNumber >= 0 && genreNumber <= 255) {
                            genreString = TagLib::ID3v1::genre(genreNumber);
                        }
                    }
                    else {
                        if (numberLength > 0) { /* genre may start with a number. */
                            if (genreString.substr(numberLength, 1) == " ") {
                                int genreNumber = genreString.substr(0, numberLength).toInt();
                                if (genreNumber >= 0 && genreNumber <= 255) {
                                    this->SetTagValue("genre", TagLib::ID3v1::genre(genreNumber), track);
                                }

                                /* strip the number */
                                genreString = genreString.substr(numberLength + 1);
                            }
                        }

                        if (!genreString.isEmpty()) {
                            this->SetTagValue("genre", genreString, track);
                        }
                    }
                }
            }
        }

        /* artists */

        this->SetSlashSeparatedValues("artist" ,allTags["TPE1"], track);
        this->SetSlashSeparatedValues("album_artist", allTags["TPE2"], track);
        this->SetSlashSeparatedValues("conductor", allTags["TPE3"], track);
        this->SetSlashSeparatedValues("interpreted", allTags["TPE4"], track);

        /* audio properties include things like bitrate, channels, and duration */

        this->SetAudioProperties(audio, track);

        /* comments, mood, and rating */

        TagLib::ID3v2::FrameList comments = allTags["COMM"];

        TagLib::ID3v2::FrameList::Iterator it = comments.begin();
        for ( ; it != comments.end(); ++it) {
            TagLib::ID3v2::CommentsFrame *comment
                = dynamic_cast<TagLib::ID3v2::CommentsFrame*> (*it);

            TagLib::String temp    = comment->description();
            std::string description(temp.begin(), temp.end());

            if (description.empty()) {
                this->SetTagValue("comment", comment->toString(), track);
            }
            else if (description.compare("MusicMatch_Mood") == 0) {
                this->SetTagValue("mood", comment->toString(), track);
            }
            else if (description.compare("MusicMatch_Preference") == 0) {
                this->SetTagValue("textrating", comment->toString(), track);
            }
        }

        /* thumbnail */

        TagLib::ID3v2::FrameList pictures = allTags["APIC"];
        if(!pictures.isEmpty()) {
            /* there can be multiple pictures, apparently. let's just use
            the first one. */

            TagLib::ID3v2::AttachedPictureFrame *picture =
                static_cast<TagLib::ID3v2::AttachedPictureFrame*>(pictures.front());

            TagLib::ByteVector pictureData = picture->picture();
            long long size = pictureData.size();

            if(size > 32) {    /* noticed that some id3tags have like a 4-8 byte size with no thumbnail */
                track->SetThumbnail(pictureData.data(), size);
            }
        }

        return true;
    }

    return false;
}

void TaglibMetadataReader::SetTagValue(
    const char* key,
    const TagLib::String tagString,
    ITagStore *track)
{
    std::string value(tagString.to8Bit(true));
    track->SetValue(key, value.c_str());
}

void TaglibMetadataReader::SetTagValue(
    const char* key,
    const char* string,
    ITagStore *track)
{
    std::string temp(string);
    track->SetValue(key, temp.c_str());
}

void TaglibMetadataReader::SetTagValue(
    const char* key, const int tagInt, ITagStore *target)
{
    std::string temp = boost::str(boost::format("%1%") % tagInt);
    target->SetValue(key, temp.c_str());
}

void TaglibMetadataReader::SetTagValues(
    const char* key,
    const TagLib::ID3v2::FrameList &frame,
    ITagStore *target)
{
    if (!frame.isEmpty()) {
        TagLib::ID3v2::FrameList::ConstIterator value = frame.begin();

        for ( ; value != frame.end(); ++value) {
            TagLib::String tagString = (*value)->toString();
            if(!tagString.isEmpty()) {
                std::string value(tagString.to8Bit(true));
                target->SetValue(key,value.c_str());
            }
        }
    }
}

void TaglibMetadataReader::SetSlashSeparatedValues(
    const char* key, TagLib::String tagString, ITagStore *track)
{
    if(!tagString.isEmpty()) {
        std::string value(tagString.to8Bit(true));
        std::vector<std::string> splitValues;

        boost::algorithm::split(splitValues, value, boost::algorithm::is_any_of("/"));

        std::vector<std::string>::iterator it = splitValues.begin();
        for( ; it != splitValues.end(); ++it) {
            track->SetValue(key, it->c_str());
        }
    }
}

void TaglibMetadataReader::SetSlashSeparatedValues(
    const char* key,
    const TagLib::ID3v2::FrameList &frame,
    ITagStore *track)
{
    if(!frame.isEmpty()) {
        TagLib::ID3v2::FrameList::ConstIterator value = frame.begin();
        for ( ; value != frame.end(); ++value) {
            TagLib::String tagString = (*value)->toString();
            this->SetSlashSeparatedValues(key, tagString, track);
        }
    }
}

void TaglibMetadataReader::SetAudioProperties(
    TagLib::AudioProperties *audioProperties, ITagStore *track)
{
    if (audioProperties) {
        std::string duration = std::to_string(audioProperties->length());
        int bitrate = audioProperties->bitrate();
        int channels = audioProperties->channels();

        this->SetTagValue("duration", duration, track);

        if (bitrate) {
            this->SetTagValue("bitrate", std::to_string(bitrate), track);
        }

        if (channels) {
            this->SetTagValue("channels", std::to_string(channels), track);
        }
    }
}
