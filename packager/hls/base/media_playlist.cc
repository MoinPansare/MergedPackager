// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/hls/base/media_playlist.h"

#include <inttypes.h>

#include <algorithm>
#include <cmath>
#include <memory>

#include "packager/base/logging.h"
#include "packager/base/strings/string_number_conversions.h"
#include "packager/base/strings/stringprintf.h"
#include "packager/file/file.h"
#include "packager/hls/base/tag.h"
#include "packager/media/base/language_utils.h"
#include "packager/media/base/muxer_util.h"
#include "packager/version/version.h"

namespace shaka {
namespace hls {

namespace {
uint32_t GetTimeScale(const MediaInfo& media_info) {
  if (media_info.has_reference_time_scale())
    return media_info.reference_time_scale();

  if (media_info.has_video_info())
    return media_info.video_info().time_scale();

  if (media_info.has_audio_info())
    return media_info.audio_info().time_scale();
  return 0u;
}

// Duplicated from MpdUtils because:
// 1. MpdUtils header depends on libxml header, which is not in the deps here
// 2. GetLanguage depends on MediaInfo from packager/mpd/
// 3. Moving GetLanguage to LanguageUtils would create a a media => mpd dep.
// TODO(https://github.com/google/shaka-packager/issues/373): Fix this
// dependency situation and factor this out to a common location.
std::string GetLanguage(const MediaInfo& media_info) {
  std::string lang;
  if (media_info.has_audio_info()) {
    lang = media_info.audio_info().language();
  } else if (media_info.has_text_info()) {
    lang = media_info.text_info().language();
  }
  return LanguageToShortestForm(lang);
}

std::string SpliceTypeToString(HlsEntry::SpliceType type) {

  if (type == HlsEntry::SpliceType::kLiveDAI)
    return "LiveDAI";
  else
  if (type == HlsEntry::SpliceType::kALTCON)
    return "ALTCON";
  else
    return "Unknown";
}

void AppendExtXMap(const MediaInfo& media_info, std::string* out) {
  if (media_info.has_init_segment_url()) {
    Tag tag("#EXT-X-MAP", out);
    tag.AddQuotedString("URI", media_info.init_segment_url().data());
    out->append("\n");
  } else if (media_info.has_media_file_url() && media_info.has_init_range()) {
    // It only makes sense for single segment media to have EXT-X-MAP if
    // there is init_range.
    Tag tag("#EXT-X-MAP", out);
    tag.AddQuotedString("URI", media_info.media_file_url().data());

    if (media_info.has_init_range()) {
      const uint64_t begin = media_info.init_range().begin();
      const uint64_t end = media_info.init_range().end();
      const uint64_t length = end - begin + 1;

      tag.AddQuotedNumberPair("BYTERANGE", length, '@', begin);
    }

    out->append("\n");
  } else {
    // This media info does not need an ext-x-map tag.
  }
}

std::string CreatePlaylistHeader(
    const MediaInfo& media_info,
    uint32_t target_duration,
    HlsPlaylistType type,
    MediaPlaylist::MediaPlaylistStreamType stream_type,
    int media_sequence_number,
    int discontinuity_sequence_number) {
  const std::string version = GetPackagerVersion();
  std::string version_line;
  if (!version.empty()) {
    version_line =
        base::StringPrintf("## Generated with %s version %s\n",
                           GetPackagerProjectUrl().c_str(), version.c_str());
  }

  // 6 is required for EXT-X-MAP without EXT-X-I-FRAMES-ONLY.
  std::string header = base::StringPrintf(
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "%s"
      "#EXT-X-TARGETDURATION:%d\n",
      version_line.c_str(), target_duration);

  switch (type) {
    case HlsPlaylistType::kVod:
      header += "#EXT-X-PLAYLIST-TYPE:VOD\n";
      break;
    case HlsPlaylistType::kEvent:
      header += "#EXT-X-PLAYLIST-TYPE:EVENT\n";
      break;
    case HlsPlaylistType::kLive:
      if (media_sequence_number > 0) {
        base::StringAppendF(&header, "#EXT-X-MEDIA-SEQUENCE:%d\n",
                            media_sequence_number);
      }
      if (discontinuity_sequence_number > 0) {
        base::StringAppendF(&header, "#EXT-X-DISCONTINUITY-SEQUENCE:%d\n",
                            discontinuity_sequence_number);
      }
      break;
    default:
      NOTREACHED() << "Unexpected MediaPlaylistType " << static_cast<int>(type);
  }
  if (stream_type ==
      MediaPlaylist::MediaPlaylistStreamType::kVideoIFramesOnly) {
    base::StringAppendF(&header, "#EXT-X-I-FRAMES-ONLY\n");
  }

  // Put EXT-X-MAP at the end since the rest of the playlist is about the
  // segment and key info.
  AppendExtXMap(media_info, &header);

  return header;
}

class SegmentInfoEntry : public HlsEntry {
 public:
  // If |use_byte_range| true then this will append EXT-X-BYTERANGE
  // after EXTINF.
  // It uses |previous_segment_end_offset| to determine if it has to also
  // specify the start byte offset in the tag.
  // |duration| is duration in seconds.
  SegmentInfoEntry(const std::string& file_name,
                   double start_time,
                   double duration,
                   bool use_byte_range,
                   uint64_t start_byte_offset,
                   uint64_t segment_file_size,
                   uint64_t previous_segment_end_offset);

  std::string ToString() override;
  double start_time() const { return start_time_; }
  double duration() const { return duration_; }
  void set_duration(double duration) { duration_ = duration; }

 private:
  SegmentInfoEntry(const SegmentInfoEntry&) = delete;
  SegmentInfoEntry& operator=(const SegmentInfoEntry&) = delete;

  const std::string file_name_;
  const double start_time_;
  double duration_;
  const bool use_byte_range_;
  const uint64_t start_byte_offset_;
  const uint64_t segment_file_size_;
  const uint64_t previous_segment_end_offset_;
};

SegmentInfoEntry::SegmentInfoEntry(const std::string& file_name,
                                   double start_time,
                                   double duration,
                                   bool use_byte_range,
                                   uint64_t start_byte_offset,
                                   uint64_t segment_file_size,
                                   uint64_t previous_segment_end_offset)
    : HlsEntry(HlsEntry::EntryType::kExtInf),
      file_name_(file_name),
      start_time_(start_time),
      duration_(duration),
      use_byte_range_(use_byte_range),
      start_byte_offset_(start_byte_offset),
      segment_file_size_(segment_file_size),
      previous_segment_end_offset_(previous_segment_end_offset) {}

std::string SegmentInfoEntry::ToString() {
  std::string result = base::StringPrintf("#EXTINF:%.3f,", duration_);

  if (use_byte_range_) {
    base::StringAppendF(&result, "\n#EXT-X-BYTERANGE:%" PRIu64,
                        segment_file_size_);
    if (previous_segment_end_offset_ + 1 != start_byte_offset_) {
      base::StringAppendF(&result, "@%" PRIu64, start_byte_offset_);
    }
  }

  base::StringAppendF(&result, "\n%s", file_name_.c_str());

  return result;
}

class EncryptionInfoEntry : public HlsEntry {
 public:
  EncryptionInfoEntry(MediaPlaylist::EncryptionMethod method,
                      const std::string& url,
                      const std::string& key_id,
                      const std::string& iv,
                      const std::string& key_format,
                      const std::string& key_format_versions);

  std::string ToString() override;

 private:
  EncryptionInfoEntry(const EncryptionInfoEntry&) = delete;
  EncryptionInfoEntry& operator=(const EncryptionInfoEntry&) = delete;

  const MediaPlaylist::EncryptionMethod method_;
  const std::string url_;
  const std::string key_id_;
  const std::string iv_;
  const std::string key_format_;
  const std::string key_format_versions_;
};

EncryptionInfoEntry::EncryptionInfoEntry(MediaPlaylist::EncryptionMethod method,
                                         const std::string& url,
                                         const std::string& key_id,
                                         const std::string& iv,
                                         const std::string& key_format,
                                         const std::string& key_format_versions)
    : HlsEntry(HlsEntry::EntryType::kExtKey),
      method_(method),
      url_(url),
      key_id_(key_id),
      iv_(iv),
      key_format_(key_format),
      key_format_versions_(key_format_versions) {}

std::string EncryptionInfoEntry::ToString() {
  std::string tag_string;
  Tag tag("#EXT-X-KEY", &tag_string);

  if (method_ == MediaPlaylist::EncryptionMethod::kSampleAes) {
    tag.AddString("METHOD", "SAMPLE-AES");
  } else if (method_ == MediaPlaylist::EncryptionMethod::kAes128) {
    tag.AddString("METHOD", "AES-128");
  } else if (method_ == MediaPlaylist::EncryptionMethod::kSampleAesCenc) {
    tag.AddString("METHOD", "SAMPLE-AES-CTR");
  } else {
    DCHECK(method_ == MediaPlaylist::EncryptionMethod::kNone);
    tag.AddString("METHOD", "NONE");
  }

  tag.AddQuotedString("URI", url_);

  if (!key_id_.empty()) {
    tag.AddString("KEYID", key_id_);
  }
  if (!iv_.empty()) {
    tag.AddString("IV", iv_);
  }
  if (!key_format_versions_.empty()) {
    tag.AddQuotedString("KEYFORMATVERSIONS", key_format_versions_);
  }
  if (!key_format_.empty()) {
    tag.AddQuotedString("KEYFORMAT", key_format_);
  }

  return tag_string;
}

class DiscontinuityEntry : public HlsEntry {
 public:
  DiscontinuityEntry();

  std::string ToString() override;

 private:
  DiscontinuityEntry(const DiscontinuityEntry&) = delete;
  DiscontinuityEntry& operator=(const DiscontinuityEntry&) = delete;
};

DiscontinuityEntry::DiscontinuityEntry()
    : HlsEntry(HlsEntry::EntryType::kExtDiscontinuity) {}

std::string DiscontinuityEntry::ToString() {
  return "#EXT-X-DISCONTINUITY";
}

class PlacementOpportunityEntry : public HlsEntry {
 public:
  PlacementOpportunityEntry();

  std::string ToString() override;

 private:
  PlacementOpportunityEntry(const PlacementOpportunityEntry&) = delete;
  PlacementOpportunityEntry& operator=(const PlacementOpportunityEntry&) =
      delete;
};

PlacementOpportunityEntry::PlacementOpportunityEntry()
    : HlsEntry(HlsEntry::EntryType::kExtPlacementOpportunity) {}

std::string PlacementOpportunityEntry::ToString() {
  return "#EXT-X-PLACEMENT-OPPORTUNITY";
}

class SignalExitEntry : public HlsEntry {
 public:

  SignalExitEntry(
    SpliceType type=SpliceType::kLiveDAI,
    double duration=hls::kDefaultValueLong,
    uint32_t eventid=hls::kDefaultValueInt,
    std::string upid="",
    uint8_t segment_type_id=hls::kDefaultValueChar,
    uint32_t flags=0,

    // these are less used parameters so putting them at end as default/optionals

    std::string signalId="",
    std::string paid="",
    uint64_t maxd=hls::kDefaultValueLong,
    uint64_t mind=hls::kDefaultValueLong,
    uint64_t maxads=hls::kDefaultValueLong,
    uint64_t minads=hls::kDefaultValueLong,
    std::string key_values=""

  ): HlsEntry(HlsEntry::EntryType::kExtSignalExit),
  spliceType_(type),
  duration_(duration),
  eventid_(eventid),
  upid_(upid),
  segment_type_id_(segment_type_id),
  flags_(flags),

  signalId_(signalId),
  paid_(paid),
  maxd_(maxd),
  mind_(mind),
  maxads_(maxads),
  minads_(minads),

  // TODO(ecl): key_values will need to be replaced by a container type
  key_values_(key_values){};


  std::string ToString() override;

 private:
  SignalExitEntry(const SignalExitEntry&) = delete;
  SignalExitEntry& operator=(const SignalExitEntry&) =
      delete;

  SpliceType spliceType_;
  double duration_;
  uint32_t eventid_;
  std::string upid_;
  uint8_t segment_type_id_;
  uint32_t flags_;

  std::string signalId_;
  std::string paid_;
  uint64_t maxd_;
  uint64_t mind_;
  uint64_t maxads_;
  uint64_t minads_;
  std::string key_values_;
};


// #EXT-X-SIGNAL-EXIT[:Duration], SpliceType=spliceType, [SignalId=signalId,] [Paid=providerID/assetID,]
// [MaxD=maxd, MinD=mind, Maxads=maxads, MinAds=minads],key1=value1,…keyN=valueN,Acds=(FW, BA)

std::string SignalExitEntry::ToString() {
  std::string tag_string;
  Tag tag("#EXT-X-SIGNAL-EXIT", &tag_string);


  if (duration_ != hls::kDefaultValueLong)
    tag.AddValue(duration_);

  tag.AddString("SpliceType",SpliceTypeToString(spliceType_));

  if (signalId_.length() != 0)
    tag.AddString("SignalId",signalId_);

  if (paid_.length() != 0)
    tag.AddString("Paid",paid_);

  if (eventid_ != hls::kDefaultValueInt)
    tag.AddNumber("segmentationEventId",eventid_);

  if (upid_.length() != 0)
    tag.AddString("segmentationUpid",upid_);

  if (segment_type_id_ != hls::kDefaultValueChar)
    tag.AddNumber("segmentationTypeId",(uint32_t)segment_type_id_);

  if (flags_) {

    tag.AddNumber("webDeliveryAllowedFlag",(uint32_t)((flags_ & kFlagWebDeliveryAllowed)>>(kFlagWebDeliveryAllowed-1)));
    tag.AddNumber("noRegionalBlackoutFlag",(uint32_t)(flags_ & kFlagNoRegionalBlackout>>(kFlagNoRegionalBlackout-1)));
    tag.AddNumber("archiveAllowedFlag",(uint32_t)(flags_ & kFlagArchiveAllowed>>(kFlagArchiveAllowed-1)));
    tag.AddNumber("deviceRestrictions",(uint32_t)(flags_ & kFlagDeviceRestrictions>>(kFlagDeviceRestrictions-1)));
  }

  if (maxd_ != hls::kDefaultValueLong)
    tag.AddNumber("MaxD",maxd_);

  if (mind_ != hls::kDefaultValueLong)
    tag.AddNumber("MinD",mind_);

  if (maxads_ != hls::kDefaultValueLong)
    tag.AddNumber("MaxAds",maxads_);

  if (minads_ != hls::kDefaultValueLong)
    tag.AddNumber("MinAds",minads_);

  // TODO(ecl): Parse the key_value container for and append the key=value list


  return tag_string;
}

class SignalSpanEntry : public HlsEntry {
 public:
  SignalSpanEntry(
    SpliceType type=SpliceType::kLiveDAI,
    double position=0,
    double duration=hls::kDefaultValueLong,
    std::string signalId="",
    std::string paid="",
    uint64_t maxd=hls::kDefaultValueLong,
    uint64_t mind=hls::kDefaultValueLong,
    uint64_t maxads=hls::kDefaultValueLong,
    uint64_t minads=hls::kDefaultValueLong,
    std::string key_values=""
  ): HlsEntry(HlsEntry::EntryType::kExtSignalSpan),
  spliceType_(type),
  position_(position),
  duration_(duration),
  signalId_(signalId),
  paid_(paid),
  maxd_(maxd),
  mind_(mind),
  maxads_(maxads),
  minads_(minads),
  key_values_(key_values){};

  std::string ToString() override;

 private:
  SignalSpanEntry(const SignalSpanEntry&) = delete;
  SignalSpanEntry& operator=(const SignalSpanEntry&) =
      delete;

  SpliceType spliceType_;
  double position_;
  double duration_;
  std::string signalId_;
  std::string paid_;
  uint64_t maxd_;
  uint64_t mind_;
  uint64_t maxads_;
  uint64_t minads_;
  std::string key_values_;
};

// #EXT-X-SIGNAL-SPAN:SecondsFromSignal[/Duration], SpliceType=spliceType, [SignalId=signalId,]
// [Paid=providerId/assetId,] [MaxD=maxd, MinD=mind, MaxAds=maxads, MinAds=minads,]
// key1=value1,…keyN=valueN,Acds=(FW, BA)

std::string SignalSpanEntry::ToString() {
  std::string tag_string;
  Tag tag("#EXT-X-SIGNAL-SPAN", &tag_string);

  tag.AddValue(position_);

  if (duration_ != hls::kDefaultValueLong) {

    tag.AddOfValue(duration_);
  }

  tag.AddString("SpliceType",SpliceTypeToString(spliceType_));

  if (signalId_.length() != 0)
    tag.AddString("SignalId",signalId_);

  if (paid_.length() != 0)
    tag.AddString("Paid",paid_);

  if (maxd_ != hls::kDefaultValueLong)
    tag.AddNumber("MaxD",maxd_);

  if (mind_ != hls::kDefaultValueLong)
    tag.AddNumber("MinD",mind_);

  if (maxads_ != hls::kDefaultValueLong)
    tag.AddNumber("MaxAds",maxads_);

  if (minads_ != hls::kDefaultValueLong)
    tag.AddNumber("MinAds",minads_);






  return tag_string;
}

class SignalReturnEntry : public HlsEntry {
 public:
  SignalReturnEntry(
    SpliceType type=SpliceType::kLiveDAI,
    double duration=hls::kDefaultValueLong
  ): HlsEntry(HlsEntry::EntryType::kExtSignalReturn),
  spliceType_(type),
  duration_(duration);

  std::string ToString() override;

 private:
  SignalReturnEntry(const SignalReturnEntry&) = delete;
  SignalReturnEntry& operator=(const SignalReturnEntry&) =
      delete;

  SpliceType spliceType_;
  double duration_;
};


std::string SignalReturnEntry::ToString() {
  std::string tag_string;
  Tag tag("#EXT-X-SIGNAL-RETURN", &tag_string);

  if (duration_ != hls::kDefaultValueLong)
    tag.AddValue(duration_);

  tag.AddString("SpliceType",SpliceTypeToString(spliceType_));

  return tag_string;
}


double LatestSegmentStartTime(
    const std::list<std::unique_ptr<HlsEntry>>& entries) {
  DCHECK(!entries.empty());
  for (auto iter = entries.rbegin(); iter != entries.rend(); ++iter) {
    if (iter->get()->type() == HlsEntry::EntryType::kExtInf) {
      const SegmentInfoEntry* segment_info =
          reinterpret_cast<SegmentInfoEntry*>(iter->get());
      return segment_info->start_time();
    }
  }
  return 0.0;
}

}  // namespace

HlsEntry::HlsEntry(HlsEntry::EntryType type) : type_(type) {}
HlsEntry::~HlsEntry() {}

MediaPlaylist::MediaPlaylist(const HlsParams& hls_params,
                             const std::string& file_name,
                             const std::string& name,
                             const std::string& group_id)
    : hls_params_(hls_params),
      file_name_(file_name),
      name_(name),
      group_id_(group_id),
      bandwidth_estimator_(hls_params_.target_segment_duration) {}

MediaPlaylist::~MediaPlaylist() {}

void MediaPlaylist::SetStreamTypeForTesting(
    MediaPlaylistStreamType stream_type) {
  stream_type_ = stream_type;
}

void MediaPlaylist::SetCodecForTesting(const std::string& codec) {
  codec_ = codec;
}

void MediaPlaylist::SetLanguageForTesting(const std::string& language) {
  language_ = language;
}

void MediaPlaylist::SetCharacteristicsForTesting(
    const std::vector<std::string>& characteristics) {
  characteristics_ = characteristics;
}

bool MediaPlaylist::SetMediaInfo(const MediaInfo& media_info) {
  const uint32_t time_scale = GetTimeScale(media_info);
  if (time_scale == 0) {
    LOG(ERROR) << "MediaInfo does not contain a valid timescale.";
    return false;
  }

  if (media_info.has_video_info()) {
    stream_type_ = MediaPlaylistStreamType::kVideo;
    codec_ = media_info.video_info().codec();
  } else if (media_info.has_audio_info()) {
    stream_type_ = MediaPlaylistStreamType::kAudio;
    codec_ = media_info.audio_info().codec();
  } else {
    stream_type_ = MediaPlaylistStreamType::kSubtitle;
    codec_ = media_info.text_info().codec();
  }

  time_scale_ = time_scale;
  media_info_ = media_info;
  language_ = GetLanguage(media_info);
  use_byte_range_ = !media_info_.has_segment_template_url();
  characteristics_ =
      std::vector<std::string>(media_info_.hls_characteristics().begin(),
                               media_info_.hls_characteristics().end());
  return true;
}

void MediaPlaylist::AddSegment(const std::string& file_name,
                               int64_t start_time,
                               int64_t duration,
                               uint64_t start_byte_offset,
                               uint64_t size) {
  if (stream_type_ == MediaPlaylistStreamType::kVideoIFramesOnly) {
    if (key_frames_.empty())
      return;

    if ((duration/time_scale_) < 1.0)
      LOG(WARNING) << "segment duration is less than 1 second. Segment merge currently not implemented!";

    AdjustLastSegmentInfoEntryDuration(key_frames_.front().timestamp);

    for (auto iter = key_frames_.begin(); iter != key_frames_.end(); ++iter) {
      // Last entry duration may be adjusted later when the next iframe becomes
      // available.
      const int64_t next_timestamp = std::next(iter) == key_frames_.end()
                                         ? (start_time + duration)
                                         : std::next(iter)->timestamp;
      AddSegmentInfoEntry(file_name, iter->timestamp,
                          next_timestamp - iter->timestamp,
                          iter->start_byte_offset, iter->size);
    }
    key_frames_.clear();
    return;
  }
  return AddSegmentInfoEntry(file_name, start_time, duration, start_byte_offset,
                             size);
}

void MediaPlaylist::AddKeyFrame(int64_t timestamp,
                                uint64_t start_byte_offset,
                                uint64_t size) {
  if (stream_type_ != MediaPlaylistStreamType::kVideoIFramesOnly) {
    if (stream_type_ != MediaPlaylistStreamType::kVideo) {
      LOG(WARNING)
          << "I-Frames Only playlist applies to video renditions only.";
      return;
    }
    stream_type_ = MediaPlaylistStreamType::kVideoIFramesOnly;
    use_byte_range_ = true;
  }
  key_frames_.push_back({timestamp, start_byte_offset, size});
}

void MediaPlaylist::AddEncryptionInfo(MediaPlaylist::EncryptionMethod method,
                                      const std::string& url,
                                      const std::string& key_id,
                                      const std::string& iv,
                                      const std::string& key_format,
                                      const std::string& key_format_versions) {
  if (!inserted_discontinuity_tag_) {
    // Insert discontinuity tag only for the first EXT-X-KEY, only if there
    // are non-encrypted media segments.
    if (!entries_.empty())
      entries_.emplace_back(new DiscontinuityEntry());
    inserted_discontinuity_tag_ = true;
  }
  entries_.emplace_back(new EncryptionInfoEntry(
      method, url, key_id, iv, key_format, key_format_versions));
}

void MediaPlaylist::AddPlacementOpportunity() {

  entries_.emplace_back(new PlacementOpportunityEntry());
}

/*
    SpliceType type=SpliceType::kLiveDAI,
    uint64_t duration=hls::kDefaultValueLong,
    std::string signalId="",
    std::string paid="",
    uint64_t maxd=hls::kDefaultValueLong,
    uint64_t mind=hls::kDefaultValueLong,
    uint64_t maxads=hls::kDefaultValueLong,
    uint64_t minads=hls::kDefaultValueLong,
    std::string key_values=""


    segmentationEventId=0x12345679,
    segmentationTypeId=52,
    webDeliveryAllowedFlag=0,
    noRegionalBlackoutFlag=1,
    archiveAllowedFlag=0,
    deviceRestrictions=0,
    segmentationDuration=120.00,
    segmentationUpid=CAgBAgMEBQYHCQ==,
    Acds=FW

*/

void MediaPlaylist::AddSignalExit(
    HlsEntry::SpliceType type,
    double duration,
    uint32_t eventid,
    std::string upid,
    uint8_t seg_type_id,
    uint32_t flags
    ) {

  entries_.emplace_back(new SignalExitEntry(

    type,
    duration,
    eventid,
    upid,
    seg_type_id,
    flags
    ));

    in_ad_state_ = true;
    ad_duration_ = duration;
    ad_position_ = 0.0;
    ad_segments_ = 0;

}

void MediaPlaylist::AddSignalSpan(HlsEntry::SpliceType type, double position, double duration) {
  entries_.emplace_back(new SignalSpanEntry(type, position, duration));
}

void MediaPlaylist::AddSignalReturn(HlsEntry::SpliceType type, double duration) {
  entries_.emplace_back(new SignalReturnEntry(type,duration));
  in_ad_state_ = false;
}


bool MediaPlaylist::WriteToFile(const std::string& file_path) {
  if (!target_duration_set_) {
    SetTargetDuration(ceil(GetLongestSegmentDuration()));
  }

  std::string content = CreatePlaylistHeader(
      media_info_, target_duration_, hls_params_.playlist_type, stream_type_,
      media_sequence_number_, discontinuity_sequence_number_);

  for (const auto& entry : entries_)
    base::StringAppendF(&content, "%s\n", entry->ToString().c_str());

  if (hls_params_.playlist_type == HlsPlaylistType::kVod) {
    content += "#EXT-X-ENDLIST\n";
  }

  if (!File::WriteFileAtomically(file_path.c_str(), content)) {
    LOG(ERROR) << "Failed to write playlist to: " << file_path;
    return false;
  }
  return true;
}

uint64_t MediaPlaylist::MaxBitrate() const {
  if (media_info_.has_bandwidth())
    return media_info_.bandwidth();
  return bandwidth_estimator_.Max();
}

uint64_t MediaPlaylist::AvgBitrate() const {
  return bandwidth_estimator_.Estimate();
}

double MediaPlaylist::GetLongestSegmentDuration() const {
  return longest_segment_duration_;
}

void MediaPlaylist::SetTargetDuration(uint32_t target_duration) {
  if (target_duration_set_) {
    if (target_duration_ == target_duration)
      return;
    VLOG(1) << "Updating target duration from " << target_duration << " to "
            << target_duration_;
  }
  target_duration_ = target_duration;
  target_duration_set_ = true;
}

int MediaPlaylist::GetNumChannels() const {
  return media_info_.audio_info().num_channels();
}

bool MediaPlaylist::GetDisplayResolution(uint32_t* width,
                                         uint32_t* height) const {
  DCHECK(width);
  DCHECK(height);
  if (media_info_.has_video_info()) {
    const double pixel_aspect_ratio =
        media_info_.video_info().pixel_height() > 0
            ? static_cast<double>(media_info_.video_info().pixel_width()) /
                  media_info_.video_info().pixel_height()
            : 1.0;
    *width = static_cast<uint32_t>(media_info_.video_info().width() *
                                   pixel_aspect_ratio);
    *height = media_info_.video_info().height();
    return true;
  }
  return false;
}

std::string MediaPlaylist::GetVideoRange() const {
  // Dolby Vision (dvh1 or dvhe) is always HDR.
  if (codec_.find("dvh") == 0)
    return "PQ";

  // HLS specification:
  // https://tools.ietf.org/html/draft-pantos-hls-rfc8216bis-02#section-4.4.4.2
  switch (media_info_.video_info().transfer_characteristics()) {
    case 1:
      return "SDR";
    case 16:
    case 18:
      return "PQ";
    default:
      // Leave it empty if we do not have the transfer characteristics
      // information.
      return "";
  }
}

double MediaPlaylist::GetFrameRate() const {
  if (media_info_.video_info().frame_duration() == 0)
    return 0;
  return static_cast<double>(time_scale_) /
         media_info_.video_info().frame_duration();
}

void MediaPlaylist::AddSegmentInfoEntry(const std::string& segment_file_name,
                                        int64_t start_time,
                                        int64_t duration,
                                        uint64_t start_byte_offset,
                                        uint64_t size) {

  if (time_scale_ == 0) {
    LOG(WARNING) << "Timescale is not set and the duration for " << duration
                 << " cannot be calculated. The output will be wrong.";

    entries_.emplace_back(new SegmentInfoEntry(
        segment_file_name, 0.0, 0.0, use_byte_range_, start_byte_offset, size,
        previous_segment_end_offset_));
    return;
  }

  // If in the ad insertion state and this is not the first segment, insert the span tag
  if (in_ad_state_) {

      if (ad_segments_ > 0) {
        // use the ad duration from the cue_event signal. The duration parmeter is the segment duration in pts time
        AddSignalSpan(HlsEntry::SpliceType::kLiveDAI, ad_position_,ad_duration_);
      }
      // track the stream position
      ad_position_ += (duration/time_scale_);
  }


  const double start_time_seconds =
      static_cast<double>(start_time) / time_scale_;
  const double segment_duration_seconds =
      static_cast<double>(duration) / time_scale_;
  longest_segment_duration_ =
      std::max(longest_segment_duration_, segment_duration_seconds);
  bandwidth_estimator_.AddBlock(size, segment_duration_seconds);

  entries_.emplace_back(new SegmentInfoEntry(
      segment_file_name, start_time_seconds, segment_duration_seconds,
      use_byte_range_, start_byte_offset, size, previous_segment_end_offset_));
  previous_segment_end_offset_ = start_byte_offset + size - 1;
  ++ad_segments_;
  SlideWindow();
}

void MediaPlaylist::AdjustLastSegmentInfoEntryDuration(int64_t next_timestamp) {
  if (time_scale_ == 0)
    return;

  const double next_timestamp_seconds =
      static_cast<double>(next_timestamp) / time_scale_;

  for (auto iter = entries_.rbegin(); iter != entries_.rend(); ++iter) {
    if (iter->get()->type() == HlsEntry::EntryType::kExtInf) {
      SegmentInfoEntry* segment_info =
          reinterpret_cast<SegmentInfoEntry*>(iter->get());

      const double segment_duration_seconds =
          next_timestamp_seconds - segment_info->start_time();
      segment_info->set_duration(segment_duration_seconds);
      longest_segment_duration_ =
          std::max(longest_segment_duration_, segment_duration_seconds);
      break;
    }
  }
}

void MediaPlaylist::SlideWindow() {

  DCHECK(!entries_.empty());
  if (hls_params_.time_shift_buffer_depth <= 0.0 ||
      hls_params_.playlist_type != HlsPlaylistType::kLive) {
    return;
  }
  DCHECK_GT(time_scale_, 0u);

  // The start time of the latest segment is considered the current_play_time,
  // and this should guarantee that the latest segment will stay in the list.
  const double current_play_time = LatestSegmentStartTime(entries_);
  if (current_play_time <= hls_params_.time_shift_buffer_depth)
    return;

  const double timeshift_limit =
      current_play_time - hls_params_.time_shift_buffer_depth;

  // Temporary list to hold the EXT-X-KEYs. For example, this allows us to
  // remove <3> without removing <1> and <2> below (<1> and <2> are moved to the
  // temporary list and added back later).
  //    #EXT-X-KEY   <1>
  //    #EXT-X-KEY   <2>
  //    #EXTINF      <3>
  //    #EXTINF      <4>
  std::list<std::unique_ptr<HlsEntry>> ext_x_keys;
  // Consecutive key entries are either fully removed or not removed at all.
  // Keep track of entry types so we know if it is consecutive key entries.
  HlsEntry::EntryType prev_entry_type = HlsEntry::EntryType::kExtInf;

  std::list<std::unique_ptr<HlsEntry>>::iterator last = entries_.begin();
  for (; last != entries_.end(); ++last) {
    HlsEntry::EntryType entry_type = last->get()->type();
    if (entry_type == HlsEntry::EntryType::kExtKey) {
      if (prev_entry_type != HlsEntry::EntryType::kExtKey)
        ext_x_keys.clear();
      ext_x_keys.push_back(std::move(*last));
    } else if (entry_type == HlsEntry::EntryType::kExtDiscontinuity) {
      ++discontinuity_sequence_number_;
    // TODO(ecl): Noop for signal events for now
    } else if (entry_type == HlsEntry::EntryType::kExtSignalExit) {
    }
    else if (entry_type == HlsEntry::EntryType::kExtSignalReturn) {
    }
    else if (entry_type == HlsEntry::EntryType::kExtSignalSpan) {
    }
    else {
      DCHECK_EQ(entry_type, HlsEntry::EntryType::kExtInf);
      const SegmentInfoEntry& segment_info =
          *reinterpret_cast<SegmentInfoEntry*>(last->get());
      const double last_segment_end_time =
          segment_info.start_time() + segment_info.duration();
      if (timeshift_limit < last_segment_end_time)
        break;
      RemoveOldSegment(segment_info.start_time());
      media_sequence_number_++;
    }
    prev_entry_type = entry_type;
  }
  entries_.erase(entries_.begin(), last);
  // Add key entries back.
  entries_.insert(entries_.begin(), std::make_move_iterator(ext_x_keys.begin()),
                  std::make_move_iterator(ext_x_keys.end()));
}

void MediaPlaylist::RemoveOldSegment(int64_t start_time) {
  if (hls_params_.preserved_segments_outside_live_window == 0)
    return;
  if (stream_type_ == MediaPlaylistStreamType::kVideoIFramesOnly)
    return;

  segments_to_be_removed_.push_back(
      media::GetSegmentName(media_info_.segment_template(), start_time,
                            media_sequence_number_, media_info_.bandwidth()));
  while (segments_to_be_removed_.size() >
         hls_params_.preserved_segments_outside_live_window) {
    VLOG(2) << "Deleting " << segments_to_be_removed_.front();
    File::Delete(segments_to_be_removed_.front().c_str());
    segments_to_be_removed_.pop_front();
  }
}

}  // namespace hls
}  // namespace shaka
