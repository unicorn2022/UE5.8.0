// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:flutter/widgets.dart';

import '../l10n/generated/tentacle_localizations.dart';
import '../platform/tentacle_api.g.dart';

/// Given a [productId], get the localized string to describe it in the given [context].
String getNameForTentacleProduct(BuildContext context, TentacleProductId productId) {
  final localizations = TentacleLocalizations.of(context)!;

  return switch (productId) {
    TentacleProductId.syncE => localizations.tentacleProductIdSyncE,
    TentacleProductId.trackE => localizations.tentacleProductIdTrackE,
    TentacleProductId.timebar => localizations.tentacleProductIdTimebar,
    _ => localizations.tentacleProductIdGeneric,
  };
}
