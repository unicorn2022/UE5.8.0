// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';

import '../../models/device.dart';
import '../../platform/tentacle_api.g.dart';

/// List of generic icon names available on every device.
final List<String> _genericIconNames = [
  'arri_alexa',
  'arri_alexa_mini',
  'arri_sr3',
  'red_komodo',
  'blackmagic_pocket_cinema_6k',
  'blackmagic_mini_ursa_pro',
  'canon_5d',
  'canon_c300',
  'denecke_ts3',
  'gopro_4',
  'iphone_2',
  'panasonic_dvx200',
  'panasonic_gh4',
  'red_dragon',
  'sony_alpha_7s',
  'sony_f55',
  'sony_fs7',
  'sony_pdw700',
  'sounddevices_633',
  'sounddevices_774t',
  'zoom_f6',
  'sounddevices_mixpre3',
  'sounddevices_mixpre6',
  'sounddevices_tascam_dr70',
  'sounddevices_tascam_hsp82',
  'zoom_f4',
  'zoom_f8',
  'zoom_h4n',
  'zoom_h6',
  'gimble',
  'zoom_f3',
  'sony_venice',
  'tonangel',
  'sigma_fp',
  'red_ranger',
];

/// Map from Tentacle product ID to the list of device icon names in indexed order.
final Map<TentacleProductId, List<String>> _iconNames = {
  TentacleProductId.syncE: [
    'sync_e_red',
    'sync_e_orange',
    'sync_e_yellow',
    'sync_e_green',
    'sync_e_turkis',
    'sync_e_blue',
    'sync_e_pink',
    'sync_e_white',
    'sync_e_black',
    ..._genericIconNames,
  ],
  TentacleProductId.trackE: [
    'track_e_orange',
    'track_e_yellow',
    'track_e_green_dark',
    'track_e_blue_light',
    'track_e_blue_dark',
    'track_e_lilac',
    'track_e_pink',
    'track_e_black',
    'track_e_gray',
    ..._genericIconNames,
  ],
  TentacleProductId.timebar: [
    'timebar_1',
    'timebar_2',
    ..._genericIconNames,
  ]
};

class TentacleDeviceIcon extends StatelessWidget {
  const TentacleDeviceIcon({
    super.key,
    required this.device,
    this.size,
    this.width,
    this.height,
    this.color,
    this.fit,
  });

  /// The Tentacle device to represent.
  final TentacleDevice device;

  /// If provided, use this as both the [width] and the [height].
  /// If no size (including [width] and [height]) are provided, this will use the default font size for its context.
  final double? size;

  /// The width of the icon.
  final double? width;

  /// The height of the icon.
  final double? height;

  /// The color of the icon.
  final Color? color;

  /// How to inscribe the image into the space allocated during layout.
  final BoxFit? fit;

  @override
  Widget build(BuildContext context) {
    return AssetIcon(
      path: 'packages/flutter_tentacle/assets/icons/tentacle/device_icon_${_getIconName()}.png',
      size: size,
      width: width,
      height: height,
      color: color,
      fit: fit,
    );
  }

  /// Get the path to the icon depending on the device.
  String _getIconName() {
    final List<String>? iconNamesForProduct = _iconNames[device.info.productId];

    if (iconNamesForProduct == null) {
      return _iconNames[TentacleProductId.syncE]!.first;
    }

    if (device.info.iconIndex >= iconNamesForProduct.length) {
      return iconNamesForProduct.first;
    }

    return iconNamesForProduct[device.info.iconIndex];
  }
}
