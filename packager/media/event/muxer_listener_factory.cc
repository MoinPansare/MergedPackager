// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/event/muxer_listener_factory.h"

#include "packager/base/strings/stringprintf.h"
#include "packager/hls/base/hls_notifier.h"
#include "packager/media/event/combined_muxer_listener.h"
#include "packager/media/event/hls_notify_muxer_listener.h"
#include "packager/media/event/mpd_notify_muxer_listener.h"
#include "packager/media/event/muxer_listener.h"
#include "packager/media/event/vod_media_info_dump_muxer_listener.h"
#include "packager/mpd/base/mpd_notifier.h"

namespace shaka {
namespace media {
namespace {
const char kMediaInfoSuffix[] = ".media_info";

std::unique_ptr<MuxerListener> CreateMediaInfoDumpListenerInternal(
    const std::string& output) {
  DCHECK(!output.empty());

  std::unique_ptr<MuxerListener> listener(
      new VodMediaInfoDumpMuxerListener(output + kMediaInfoSuffix));
  return listener;
}

std::unique_ptr<MuxerListener> CreateMpdListenerInternal(
    MpdNotifier* notifier) {
  DCHECK(notifier);

  std::unique_ptr<MuxerListener> listener(new MpdNotifyMuxerListener(notifier));
  return listener;
}

std::list<std::unique_ptr<MuxerListener>> CreateHlsListenersInternal(
    const MuxerListenerFactory::StreamData& stream,
    int stream_index,
    hls::HlsNotifier* notifier) {
  DCHECK(notifier);
  DCHECK_GE(stream_index, 0);

  std::string name = stream.hls_name;
  std::string playlist_name = stream.hls_playlist_name;

  const std::string& group_id = stream.hls_group_id;
  const std::string& iframe_playlist_name = stream.hls_iframe_playlist_name;
  const std::vector<std::string>& characteristics = stream.hls_characteristics;

  if (name.empty()) {
    name = base::StringPrintf("stream_%d", stream_index);
  }

  if (playlist_name.empty()) {
    playlist_name = base::StringPrintf("stream_%d.m3u8", stream_index);
  }

  const bool kIFramesOnly = true;
  std::list<std::unique_ptr<MuxerListener>> listeners;
  listeners.emplace_back(new HlsNotifyMuxerListener(
      playlist_name, !kIFramesOnly, name, group_id, characteristics, notifier));
  if (!iframe_playlist_name.empty()) {
    listeners.emplace_back(new HlsNotifyMuxerListener(
        iframe_playlist_name, kIFramesOnly, name, group_id,
        std::vector<std::string>(), notifier));
  }
  return listeners;
}
}  // namespace

MuxerListenerFactory::MuxerListenerFactory(bool output_media_info,
                                           MpdNotifier* mpd_notifier,
                                           hls::HlsNotifier* hls_notifier)
    : output_media_info_(output_media_info),
      mpd_notifier_(mpd_notifier),
      hls_notifier_(hls_notifier) {}

std::unique_ptr<MuxerListener> MuxerListenerFactory::CreateListener(
    const StreamData& stream) {
  const int stream_index = stream_index_++;

  std::unique_ptr<CombinedMuxerListener> combined_listener(
      new CombinedMuxerListener);

  if (output_media_info_) {
    combined_listener->AddListener(
        CreateMediaInfoDumpListenerInternal(stream.media_info_output));
  }
  if (mpd_notifier_) {
    combined_listener->AddListener(CreateMpdListenerInternal(mpd_notifier_));
  }
  if (hls_notifier_) {
    for (auto& listener :
         CreateHlsListenersInternal(stream, stream_index, hls_notifier_)) {
      combined_listener->AddListener(std::move(listener));
    }
  }

  return std::move(combined_listener);
}

std::unique_ptr<MuxerListener> MuxerListenerFactory::CreateHlsListener(
    const StreamData& stream) {
  if (!hls_notifier_) {
    return nullptr;
  }

  const int stream_index = stream_index_++;
  return std::move(
      CreateHlsListenersInternal(stream, stream_index, hls_notifier_).front());
}

}  // namespace media
}  // namespace shaka
