// -*- mode: cpp; indent-tabs-mode: nil; c-basic-offset: 2 -*-
//
// This file is part of Genie
//
// Copyright 2021 The Board of Trustees of the Leland Stanford Junior University
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <glib-unix.h>
#include <glib.h>

#include "app.hpp"
#include "audioinput.hpp"
#include "audiofifo.hpp"
#include "audioplayer.hpp"
#include "config.hpp"
#include "evinput.hpp"
#include "leds.hpp"
#include "spotifyd.hpp"
#include "stt.hpp"
#include "conversation_client.hpp"
#include "dns_controller.hpp"

double time_diff(struct timeval x, struct timeval y) {
  return (((double)y.tv_sec * 1000000 + (double)y.tv_usec) -
          ((double)x.tv_sec * 1000000 + (double)x.tv_usec));
}

double time_diff_ms(struct timeval x, struct timeval y) {
  return time_diff(x, y) / 1000;
}

genie::App::App() {
  isProcessing = FALSE;

  // initialize a shared SoupSession to be used by all outgoing connections
  m_soup_session = auto_gobject_ptr<SoupSession>(soup_session_new(), adopt_mode::owned);
  // enable the wss support
  const gchar *wss_aliases[] = {"wss", NULL};
  g_object_set(m_soup_session.get(), SOUP_SESSION_HTTPS_ALIASES, wss_aliases, NULL);
}

genie::App::~App() { g_main_loop_unref(main_loop); }

int genie::App::exec() {
  PROF_PRINT("start main loop\n");

  main_loop = g_main_loop_new(NULL, FALSE);

  g_unix_signal_add(SIGINT, sig_handler, main_loop);
  g_unix_signal_add(SIGTERM, sig_handler, main_loop);

  m_config = std::make_unique<Config>();
  m_config->load();

  g_setenv("GST_REGISTRY_UPDATE", "no", true);

  m_audioFIFO = std::make_unique<AudioFIFO>(this);
  m_audioFIFO->init();

  m_audioPlayer = std::make_unique<AudioPlayer>(this);

  m_stt = std::make_unique<STT>(this);

  m_audioInput = std::make_unique<AudioInput>(this);
  m_audioInput->init();

  m_spotifyd = std::make_unique<Spotifyd>(this);
  m_spotifyd->init();

  conversation_client = std::make_unique<ConversationClient>(this);
  conversation_client->init();

  m_evInput = std::make_unique<EVInput>(this);
  m_evInput->init();

  m_leds = std::make_unique<Leds>(this);
  m_leds->init();
  m_leds->set_active(false);

  if (m_config->dns_controller_enabled)
    m_dns_controller = std::make_unique<DNSController>();
  
  m_state_machine = std::make_unique<state::Machine>(this);
  m_state_machine->init<state::Sleeping>();

  g_debug("start main loop\n");
  g_main_loop_run(main_loop);
  g_debug("main loop returned\n");

  return 0;
}

gboolean genie::App::sig_handler(gpointer data) {
  GMainLoop *loop = reinterpret_cast<GMainLoop *>(data);
  g_main_loop_quit(loop);
  return G_SOURCE_REMOVE;
}

void genie::App::duck() {
  system("amixer -D hw:audiocodec cset name='hd' 128");
}

void genie::App::unduck() {
  system("amixer -D hw:audiocodec cset name='hd' 255");
}

void genie::App::print_processing_entry(const char *name, double duration_ms,
                                        double total_ms) {
  g_print("%12s: %5.3lf ms (%3d%%)\n", name, duration_ms,
          (int)((duration_ms / total_ms) * 100));
}

/**
 * @brief Track an event that is part of a turn's remote processing.
 *
 * We want to keep a close eye the performance of our
 *
 * @param eventType
 */
void genie::App::track_processing_event(ProcesingEvent_t eventType) {
  // Unless we are starting a turn or already in a turn just bail out. This
  // avoids tracking the "Hi..." and any other messages at connect.
  if (!(eventType == PROCESSING_BEGIN || isProcessing)) {
    return;
  }

  switch (eventType) {
    case PROCESSING_BEGIN:
      gettimeofday(&tStartProcessing, NULL);
      isProcessing = TRUE;
      break;
    case PROCESSING_START_STT:
      gettimeofday(&tStartSTT, NULL);
      break;
    case PROCESSING_END_STT:
      gettimeofday(&tEndSTT, NULL);
      break;
    case PROCESSING_START_GENIE:
      gettimeofday(&tStartGenie, NULL);
      break;
    case PROCESSING_END_GENIE:
      gettimeofday(&tEndGenie, NULL);
      break;
    case PROCESSING_START_TTS:
      gettimeofday(&tStartTTS, NULL);
      break;
    case PROCESSING_END_TTS:
      gettimeofday(&tEndTTS, NULL);
      break;
    case PROCESSING_FINISH:
      int total_ms = time_diff_ms(tStartProcessing, tEndTTS);

      g_print("############# Processing Performance #################\n");
      print_processing_entry(
          "Pre-STT", time_diff_ms(tStartProcessing, tStartSTT), total_ms);
      print_processing_entry("STT", time_diff_ms(tStartSTT, tEndSTT), total_ms);
      print_processing_entry("STT->Genie", time_diff_ms(tEndSTT, tStartGenie),
                             total_ms);
      print_processing_entry("Genie", time_diff_ms(tStartGenie, tEndGenie),
                             total_ms);
      print_processing_entry("Genie->TTS", time_diff_ms(tEndGenie, tStartTTS),
                             total_ms);
      print_processing_entry("TTS", time_diff_ms(tStartTTS, tEndTTS), total_ms);
      g_print("------------------------------------------------------\n");
      print_processing_entry("Total", total_ms, total_ms);
      g_print("######################################################\n");

      isProcessing = FALSE;
      break;
  }
}
