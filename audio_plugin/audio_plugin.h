/*
==============================================================================

BEGIN_JUCE_MODULE_DECLARATION

   ID:            audio_plugin
   vendor:        Ankalot
   version:       0.1.0
   name:          Toposhaper
   description:   Topological waveshaper core
   dependencies:  juce_audio_utils

   license:       GPL-3.0

END_JUCE_MODULE_DECLARATION

==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "include/UiTokens.h"
#include "include/WarpCurve.h"
#include "include/WarpCurveEditor.h"
#include "include/ScopeView.h"
#include "include/FactoryPresets.h"
#include "include/PresetBar.h"
#include "include/PluginProcessor.h"
#include "include/PluginEditor.h"

// #include all additional header files below
