// Copyright (c) 2014 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

package org.cef.handler;

import org.cef.browser.CefBrowser;
import org.cef.misc.CefAudioParameters;
import org.cef.misc.DataPointer;

/**
 * Implement this interface to handle events related to audio playing.
 * The methods of this class will be called on the UI thread.
 */
public interface CefAudioHandler {
	boolean getAudioParameters(CefBrowser browser, CefAudioParameters params);
	
	void onAudioStreamStarted(CefBrowser browser, CefAudioParameters params, int channels);

	default void onAudioStreamStarted(int browserId, CefAudioParameters params, int channels) {
		onAudioStreamStarted(null, params, channels);
	}
	
	void onAudioStreamPacket(CefBrowser browser, DataPointer data, int frames, long pts);

	default void onAudioStreamPacket(int browserId, DataPointer data, int frames, long pts) {
		onAudioStreamPacket(null, data, frames, pts);
	}
	
	void onAudioStreamStopped(CefBrowser browser);

	default void onAudioStreamStopped(int browserId) {
		onAudioStreamStopped((CefBrowser) null);
	}
	
	void onAudioStreamError(CefBrowser browser, String text);

	default void onAudioStreamError(int browserId, String text) {
		onAudioStreamError(null, text);
	}
}
