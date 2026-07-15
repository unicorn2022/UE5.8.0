// Copyright Epic Games, Inc. All Rights Reserved.

using Avalonia.Controls;
using Avalonia.Interactivity;

namespace UnrealToolboxExampleApplication;

public partial class UpdateDialog : Window
{
	public UpdateDialog()
	{
		InitializeComponent();
	}

	void OnYesClicked(object? sender, RoutedEventArgs e)
	{
		Close(true);
	}

	void OnNoClicked(object? sender, RoutedEventArgs e)
	{
		Close(false);
	}
}
