#pragma once

// The platform's AAC / M4A decoder (miniaudio itself decodes only WAV, FLAC and MP3).
#ifdef _WIN32
#include "audio/MfAudioSource.h"
using AacSource = MfAudioSource;
#else
#include "audio/FfmpegAudioSource.h"
using AacSource = FfmpegAudioSource;
#endif
