// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.unreal.download.asyncdownloader;

import androidx.annotation.NonNull;

import java.io.File;
import java.net.URL;

public interface AsyncDownloadRequestListener {
	void OnStart(@NonNull String Id, @NonNull String DebugString, long StartTimeUTC);
	void OnProgress(@NonNull String Id, long BytesDownloadedSoFar);
	void OnSuccess(@NonNull String Id, @NonNull File File);
	void OnError(@NonNull String Id, @NonNull Throwable Error, URL LastTriedUrl);
	void OnPaused(@NonNull String Id);
}
