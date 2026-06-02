// Copyright (c) 2014 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "audio_handler.h"

#include "jni_util.h"

#include <fstream>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

std::mutex g_browser_cache_mutex;
std::map<int, jobject> g_browser_cache;
int g_audio_java_process_id = -1;

int AudioProcessId() {
#if defined(_WIN32)
  return _getpid();
#else
  return getpid();
#endif
}

void AudioDebugLog(const char* message, int browserId = -1) {
  std::ofstream log("cwd-jcef-audio.log", std::ios::app);
  if (log.is_open()) {
    log << message << " browser=" << browserId << " pid=" << AudioProcessId() << std::endl;
  }
}

}  // namespace

AudioHandler::AudioHandler(JNIEnv* env, jobject handler)
    : handle_(env, handler) {}

AudioHandler::~AudioHandler() {
}

jobject AudioHandler::GetCachedBrowser(JNIEnv* env, CefRefPtr<CefBrowser> browser) {
  if (!browser)
    return nullptr;

  std::lock_guard<std::mutex> lock(g_browser_cache_mutex);
  auto it = g_browser_cache.find(browser->GetIdentifier());
  return it == g_browser_cache.end() ? nullptr : it->second;
}

void AudioHandler::CacheBrowser(JNIEnv* env,
                                CefRefPtr<CefBrowser> browser,
                                jobject jbrowser) {
  if (!browser || !jbrowser)
    return;

  std::lock_guard<std::mutex> lock(g_browser_cache_mutex);
  int browserId = browser->GetIdentifier();
  if (g_browser_cache.find(browserId) != g_browser_cache.end())
    return;
  g_browser_cache[browserId] = env->NewGlobalRef(jbrowser);
}

void AudioHandler::RemoveCachedBrowser(CefRefPtr<CefBrowser> browser) {
  if (!browser)
    return;

  jobject cached = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_browser_cache_mutex);
    auto it = g_browser_cache.find(browser->GetIdentifier());
    if (it == g_browser_cache.end())
      return;
    cached = it->second;
    g_browser_cache.erase(it);
  }

  ScopedJNIEnv env(0);
  if (env && cached)
    env->DeleteGlobalRef(cached);
}

jobject jniParams(JNIEnv* env, jclass clsProps, const CefAudioParameters& params) {
  jclass cls = env->FindClass("org/cef/misc/CefChannelLayout");
  if (cls == nullptr) {
//    std::cout << "Could not find class 0";
    return nullptr;
  }
  jmethodID getLayout = env->GetStaticMethodID(cls, "forId", "(I)Lorg/cef/misc/CefChannelLayout;");
  if (getLayout == 0) {
//    std::cout << "Could not find method 0";
    return nullptr;
  }
  jobject layout = env->CallStaticObjectMethod(cls, getLayout, (int) params.channel_layout);

  cls = clsProps;
  if (cls == nullptr) {
//    std::cout << "Could not find class 1";
    return nullptr;
  }
  jmethodID constructor = env->GetMethodID(cls, "<init>", "(Lorg/cef/misc/CefChannelLayout;II)V");
  if (constructor == 0) {
//    std::cout << "Could not find constructor 1";
    return nullptr;
  }
  jobject parameters = env->NewObject(cls, constructor, layout, params.sample_rate, params.frames_per_buffer);

  return parameters;
}

jobject jniParams(JNIEnv* env, const CefAudioParameters& params) {
  jclass cls = env->FindClass("org/cef/misc/CefAudioParameters");
  return jniParams(env, cls, params);
}

bool AudioHandler::GetAudioParameters(CefRefPtr<CefBrowser> browser,
                                      CefAudioParameters& params) {
  AudioDebugLog("GetAudioParameters.enter", browser ? browser->GetIdentifier() : -1);
  ScopedJNIEnv env(0);
  if (!env) {
    AudioDebugLog("GetAudioParameters.no-env", browser ? browser->GetIdentifier() : -1);
    return true;
  }

  ScopedJNIBrowser jbrowser(env, browser);

  jboolean jreturn = JNI_FALSE;
  jclass cls = env->FindClass("org/cef/misc/CefAudioParameters");
  jobject paramsJni = jniParams(env, cls, params);

  JNI_CALL_METHOD(env, handle_, "getAudioParameters",
                       "(Lorg/cef/browser/CefBrowser;Lorg/cef/misc/CefAudioParameters;)Z", Boolean,
                       jreturn, jbrowser.get(), paramsJni);
  if (jreturn != JNI_FALSE) {
    params.channel_layout = CEF_CHANNEL_LAYOUT_STEREO;
    params.sample_rate = 44100;
    params.frames_per_buffer = 1024;
    CacheBrowser(env, browser, jbrowser.get());
    g_audio_java_process_id = AudioProcessId();
    AudioDebugLog("GetAudioParameters.return-true", browser ? browser->GetIdentifier() : -1);
  } else {
    AudioDebugLog("GetAudioParameters.return-false", browser ? browser->GetIdentifier() : -1);
  }

  return (jreturn != JNI_FALSE);
}

void AudioHandler::OnAudioStreamStarted(CefRefPtr<CefBrowser> browser,
                                        const CefAudioParameters& params, int channels) {
  AudioDebugLog("OnAudioStreamStarted.enter", browser ? browser->GetIdentifier() : -1);
  ScopedJNIEnv env(0);
  if (!env) {
    AudioDebugLog("OnAudioStreamStarted.no-env", browser ? browser->GetIdentifier() : -1);
    return;
  }
  if (g_audio_java_process_id != -1 && g_audio_java_process_id != AudioProcessId()) {
    AudioDebugLog("OnAudioStreamStarted.cross-process", browser ? browser->GetIdentifier() : -1);
    return;
  }

  ScopedJNIObjectLocal paramsJni(env, jniParams(env, params));
  AudioDebugLog(paramsJni.get() ? "OnAudioStreamStarted.params-created" : "OnAudioStreamStarted.no-params",
                browser ? browser->GetIdentifier() : -1);

  AudioDebugLog("OnAudioStreamStarted.before-call", browser ? browser->GetIdentifier() : -1);
  JNI_CALL_VOID_METHOD(env, handle_, "onAudioStreamStarted",
                       "(ILorg/cef/misc/CefAudioParameters;I)V",
                       browser ? browser->GetIdentifier() : -1, paramsJni.get(), channels);
  AudioDebugLog("OnAudioStreamStarted.called-java", browser ? browser->GetIdentifier() : -1);
}

void AudioHandler::OnAudioStreamPacket(CefRefPtr<CefBrowser> browser, const float** data, int frames, int64_t pts) {
  static int packetLogCount = 0;
  if (packetLogCount < 5) {
    AudioDebugLog("OnAudioStreamPacket.enter", browser ? browser->GetIdentifier() : -1);
    packetLogCount++;
  }
  ScopedJNIEnv env(0);
  if (!env)
    return;
  if (g_audio_java_process_id != -1 && g_audio_java_process_id != AudioProcessId())
    return;

  ScopedJNIObjectLocal dataPtr(
      env, NewJNIObject(env, "org/cef/misc/DataPointer", "(J)V", (jlong) data));

  JNI_CALL_VOID_METHOD(env, handle_, "onAudioStreamPacket",
                  "(ILorg/cef/misc/DataPointer;IJ)V",
                  browser ? browser->GetIdentifier() : -1, dataPtr.get(), frames, (long long) pts);
}

void AudioHandler::OnAudioStreamStopped(CefRefPtr<CefBrowser> browser) {
  AudioDebugLog("OnAudioStreamStopped.enter", browser ? browser->GetIdentifier() : -1);
  ScopedJNIEnv env(0);
  if (!env)
    return;
  if (g_audio_java_process_id != -1 && g_audio_java_process_id != AudioProcessId())
    return;

  JNI_CALL_VOID_METHOD(env, handle_, "onAudioStreamStopped",
                       "(I)V",
                       browser ? browser->GetIdentifier() : -1);
  RemoveCachedBrowser(browser);
}

void AudioHandler::OnAudioStreamError(CefRefPtr<CefBrowser> browser,
                                      const CefString& text) {
  AudioDebugLog("OnAudioStreamError.enter", browser ? browser->GetIdentifier() : -1);
  ScopedJNIEnv env(0);
  if (!env)
    return;
  if (g_audio_java_process_id != -1 && g_audio_java_process_id != AudioProcessId())
    return;

  ScopedJNIString jtext(env, text);

  JNI_CALL_VOID_METHOD(env, handle_, "onAudioStreamError",
                       "(ILjava/lang/String;)V",
                       browser ? browser->GetIdentifier() : -1, jtext.get());
  RemoveCachedBrowser(browser);
}
