// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:flutter/material.dart';

/// Default decoration style for text inputs.
final InputDecoration collapsedInputDecoration = const InputDecoration(
  isCollapsed: true,
  contentPadding: EdgeInsets.symmetric(horizontal: 12, vertical: 16),
  border: OutlineInputBorder(
    borderRadius: BorderRadius.all(Radius.circular(4)),
    borderSide: BorderSide.none,
  ),
);
