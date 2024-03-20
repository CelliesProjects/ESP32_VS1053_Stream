// https://github.com/yang123vc/M3u8Parser/blob/master/HLSCommon.h

#ifndef __HLSCOMMON_H__
#define __HLSCOMMON_H__

// https://datatracker.ietf.org/doc/html/rfc8216

//Basic Tags
const char* EXTM3U = "#EXTM3U";
const char* EXT_X_VERSION = "#EXT-X-VERSION";

//Media Playlist Tags
const char* EXT_X_TARGETDURATION = "#EXT-X-TARGETDURATION";
const char* EXT_X_MEDIA_SEQUENCE = "#EXT-X-MEDIA-SEQUENCE";
const char* EXT_X_DISCONTINUITY_SEQUENCE = "#EXT-X-DISCONTINUITY-SEQUENCE";
const char* EXT_X_ENDLIST = "#EXT-X-ENDLIST";
const char* EXT_X_PLAYLIST_TYPE = "#EXT-X-PLAYLIST-TYPE";
const char* EXT_X_I_FRAMES_ONLY = "#EXT-X-I-FRAMES-ONLY";

//Master Playlist Tags
const char* EXT_X_MEDIA = "#EXT-X-MEDIA";
const char* EXT_X_STREAM_INF = "#EXT-X-STREAM-INF";
const char* EXT_X_I_FRAME_STREAM_INF = "#EXT-X-I-FRAME-STREAM-INF";
const char* EXT_X_SESSION_DATA = "#EXT-X-SESSION-DATA";
const char* EXT_X_SESSION_KEY = "#EXT-X-SESSION-KEY";

//Media or Master Playlist Tags
const char* EXT_X_INDEPENDENT_SEGMENTS = "#EXT-X-INDEPENDENT-SEGMENTS";
const char* EXT_X_START = "#EXT-X-START";

//Media Segment Tags
const char* EXTINF = "#EXTINF";
const char* EXT_X_BYTERANGE = "#EXT-X-BYTERANGE";
const char* EXT_X_DISCONTINUITY = "#EXT-X-DISCONTINUITY";
const char* EXT_X_KEY = "#EXT-X-KEY";
const char* EXT_X_MAP = "#EXT-X-MAP";
const char* EXT_X_PROGRAM_DATE_TIME = "#EXT-X-PROGRAM-DATE-TIME";
const char* EXT_X_DATERANGE = "#EXT-X-DATERANGE";

#endif
