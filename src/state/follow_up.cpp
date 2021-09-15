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

#include "app.hpp"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "genie::state::FollowUp"

namespace genie {
namespace state {

FollowUp::FollowUp(Machine *machine) : State{machine} {}

void FollowUp::exit() { app->follow_up_id = -1; }

void FollowUp::react(events::PlayerStreamEnd *player_stream_end) {
  if (app->follow_up_id >= 0 &&
      app->follow_up_id == player_stream_end->ref_id) {
    machine->transit<Listening>();
  }
}

} // namespace state
} // namespace genie
