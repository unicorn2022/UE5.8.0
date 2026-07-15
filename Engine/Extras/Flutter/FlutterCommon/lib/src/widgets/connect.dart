// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:collection';

import 'package:flutter/material.dart';
import 'package:logging/logging.dart';

import '../../../localizations.dart';
import '../../../theme.dart';
import '../../../unreal_beacon.dart';
import '../../../widgets.dart';

final _log = Logger('EpicConnectScreen');

/// Check if the vertical space is limited in a build context, meaning we should use alternate versions of the connect
/// screen widgets.
bool _shouldUseShortWidgets(BuildContext context) => MediaQuery.of(context).size.height < 600;

/// Get the inner spacing to use between tiles based on the screen size.
double _getInnerSpacing(BuildContext context) => _shouldUseShortWidgets(context) ? 16 : 32;

/// Scaffold for a connection screen that automatically finds Unreal Engine instances using a beacon message.
class EpicConnectScreen<BeaconDataType, ConnectionDataType> extends StatefulWidget {
  const EpicConnectScreen({
    required this.createBeacon,
    required this.onManualConnectPressed,
    required this.mostRecentTileBuilder,
    required this.beaconTileBuilder,
    Key? key,
  }) : super(key: key);

  /// Function that creates the beacon to find Unreal Engine instances on the network.
  /// Takes a [context] and [onBeaconFailure] which should be passed to the beacon.
  final UnrealEngineBeacon<BeaconDataType> Function(
    BuildContext context,
    VoidCallback onBeaconFailure,
  ) createBeacon;

  /// Callback function for when the manual connection button is pressed.
  final VoidCallback onManualConnectPressed;

  /// Function that creates the "most recent" connection tile in the current build [context].
  final Widget? Function(BuildContext context) mostRecentTileBuilder;

  /// Function that creates a tile to connect based on the given [beaconResponse] in the current build [context].
  final Widget? Function(BuildContext context, UnrealBeaconResponse<BeaconDataType> beaconResponse) beaconTileBuilder;

  @override
  _EpicConnectScreenState createState() => _EpicConnectScreenState<BeaconDataType, ConnectionDataType>();
}

class _EpicConnectScreenState<BeaconDataType, ConnectionDataType> extends State<EpicConnectScreen>
    with WidgetsBindingObserver {
  /// Beacon which will be used to detect instances of Unreal Engine.
  UnrealEngineBeacon<BeaconDataType>? _engineBeacon;

  /// Whether a failure message is already visible.
  bool _bIsFailureMessageVisible = false;

  @override
  void initState() {
    super.initState();

    WidgetsBinding.instance.addObserver(this);

    _createBeacon();
  }

  @override
  void didUpdateWidget(covariant EpicConnectScreen oldWidget) {
    super.didUpdateWidget(oldWidget);

    if (oldWidget.createBeacon != widget.createBeacon) {
      _createBeacon();
    }
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);

    _engineBeacon?.dispose();

    super.dispose();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    switch (state) {
      case AppLifecycleState.resumed:
        _engineBeacon?.resume();
        break;

      case AppLifecycleState.paused:
        // If the app is in the background, stop sending beacon messages
        _engineBeacon?.pause();
        break;

      default:
        break;
    }
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      color: UnrealColors.gray14,
      padding: EdgeInsets.only(
        left: UnrealTheme.cardMargin,
        right: UnrealTheme.cardMargin,
        bottom: UnrealTheme.cardMargin,
      ),
      child: Container(
        width: MediaQuery.of(context).size.width,
        padding: EdgeInsets.all(UnrealTheme.cardMargin),
        decoration: BoxDecoration(
          color: Theme.of(context).colorScheme.background,
          borderRadius: BorderRadius.circular(UnrealTheme.outerCornerRadius),
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Card(
              child: IntrinsicWidth(
                child: Column(
                  children: [
                    CardLargeHeader(
                      title: EpicCommonLocalizations.of(context)!.connectScreenQuickConnectPanelTitle,
                      iconPath: 'packages/epic_common/assets/icons/light_card.svg',
                    ),
                    Expanded(
                      child: _QuickConnectGrid(
                        mostRecentTileBuilder: widget.mostRecentTileBuilder,
                        onManualConnectPressed: widget.onManualConnectPressed,
                      ),
                    ),
                  ],
                ),
              ),
            ),
            SizedBox(width: UnrealTheme.cardMargin),
            Expanded(
              child: Card(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    CardLargeHeader(
                      title: EpicCommonLocalizations.of(context)!.connectScreenAllConnectionsPanelTitle,
                      iconPath: 'packages/epic_common/assets/icons/unreal_u_logo.svg',
                    ),
                    Expanded(
                      child: _AllConnectionsGrid<BeaconDataType, ConnectionDataType>(
                        engineBeacon: _engineBeacon!,
                        beaconTileBuilder: widget.beaconTileBuilder,
                        onManualConnectPressed: widget.onManualConnectPressed,
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  /// Create the beacon using the function passed to the widget.
  void _createBeacon() {
    _engineBeacon?.dispose();
    _engineBeacon = widget.createBeacon(
      context,
      _onBeaconFailure,
    ) as UnrealEngineBeacon<BeaconDataType>;
  }

  /// Called when the beacon has failed for too long.
  void _onBeaconFailure() {
    if (!_bIsFailureMessageVisible) {
      _bIsFailureMessageVisible = true;
      InfoModalDialog.show(context, EpicCommonLocalizations.of(context)!.connectScreenBeaconFailedMessage)
          .then((value) => _bIsFailureMessageVisible = false);
    }
  }
}

/// Quick connect column showing most recent connection instances of UE.
class _QuickConnectGrid<ConnectionDataType> extends StatelessWidget {
  const _QuickConnectGrid({
    Key? key,
    required this.mostRecentTileBuilder,
    required this.onManualConnectPressed,
  }) : super(key: key);

  /// Function that creates the "most recent" connection tile in the current build [context].
  final Widget? Function(BuildContext context) mostRecentTileBuilder;

  /// Callback function for when the manual connection button is pressed.
  final VoidCallback onManualConnectPressed;

  @override
  Widget build(BuildContext context) {
    final double spacing = _getInnerSpacing(context);
    final Widget? mostRecentTile = mostRecentTileBuilder(context);

    return Padding(
      padding: EdgeInsets.all(spacing),
      child: SingleChildScrollView(
        child: Column(children: [
          if (mostRecentTile != null)
            Padding(
              padding: EdgeInsets.only(bottom: spacing),
              child: mostRecentTile,
            ),
          ConnectionTile(
            onTap: onManualConnectPressed,
            primaryText: EpicCommonLocalizations.of(context)!.connectScreenNewConnectionButtonTitle,
            emphasisText: EpicCommonLocalizations.of(context)!.connectScreenNewConnectionButtonLabel,
            bWrapPrimaryText: true,
          ),
        ]),
      ),
    );
  }
}

/// Grid of currently visible Unreal Engine instances.
class _AllConnectionsGrid<BeaconDataType, ConnectionDataType> extends StatelessWidget {
  const _AllConnectionsGrid({
    super.key,
    required this.engineBeacon,
    required this.beaconTileBuilder,
    required this.onManualConnectPressed,
  });

  /// The beacon used to detect engine instances.
  final UnrealEngineBeacon<BeaconDataType> engineBeacon;

  /// Function that creates a tile to connect based on the given [beaconResponse] in the current build [context].
  final Widget? Function(BuildContext context, UnrealBeaconResponse<BeaconDataType> beaconResponse) beaconTileBuilder;

  /// Callback function for when the manual connection button is pressed.
  final VoidCallback onManualConnectPressed;

  @override
  Widget build(BuildContext context) {
    final double spacing = _getInnerSpacing(context);

    return ValueListenableBuilder<UnmodifiableListView<UnrealBeaconResponse<BeaconDataType>>>(
      valueListenable: engineBeacon.responses,
      builder: (_, responses, __) {
        if (responses.isEmpty) {
          return EmptyPlaceholder(
            message: EpicCommonLocalizations.of(context)!.connectScreenAllConnectionsPanelEmptyMessage,
            button: EpicWideButton(
              text: EpicCommonLocalizations.of(context)!.connectScreenAllConnectionsPanelEmptyButtonLabel,
              iconPath: 'packages/epic_common/assets/icons/plus.svg',
              color: UnrealColors.highlightGreen,
              onPressed: onManualConnectPressed,
            ),
          );
        }

        return SingleChildScrollView(
          padding: EdgeInsets.all(spacing),
          child: Wrap(
            direction: Axis.horizontal,
            alignment: WrapAlignment.center,
            spacing: spacing,
            runSpacing: spacing,
            children:
                responses.map((response) => beaconTileBuilder(context, response)).nonNulls.toList(growable: false),
          ),
        );
      },
    );
  }
}

/// Tile showing an Unreal Engine instance available for connection.
class ConnectionTile extends StatelessWidget {
  const ConnectionTile({
    Key? key,
    required this.primaryText,
    required this.onTap,
    this.secondaryText,
    this.emphasisText,
    this.fillColor = Colors.transparent,
    this.bWrapPrimaryText = false,
    this.bWrapSecondaryText = false,
    this.bWrapEmphasisText = false,
  }) : super(key: key);

  /// The top piece of text shown on the tile.
  final String primaryText;

  /// Function called when this tile is tapped.
  final VoidCallback onTap;

  /// Optional second piece of text shown on the tile.
  final String? secondaryText;

  /// Optional text shown under the other two with an emphasized font.
  final String? emphasisText;

  /// The color with which to fill the tile. If transparent, text will be colored accordingly.
  final Color fillColor;

  /// If true, split the [primaryText] into multiple lines if it overflows.
  final bool bWrapPrimaryText;

  /// If true, split the [secondaryText] into multiple lines if it overflows.
  final bool bWrapSecondaryText;

  /// If true, split the [emphasisText] into multiple lines if it overflows.
  final bool bWrapEmphasisText;

  @override
  Widget build(BuildContext context) {
    final bool bIsWide = _shouldUseShortWidgets(context);

    // Set up fill + text color
    final bool bIsFilled = fillColor != Colors.transparent;
    late final Color textColor = bIsFilled ? Theme.of(context).colorScheme.onPrimary : UnrealColors.gray56;

    // The Unreal icon
    final unrealIcon = AssetIcon(
      path: 'packages/epic_common/assets/icons/unreal_u.svg',
      size: 50,
      color: bIsFilled ? UnrealColors.white : UnrealColors.gray75,
    );

    // The column containing connection data
    final dataColumn = Padding(
      padding: EdgeInsets.symmetric(horizontal: 5),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        crossAxisAlignment: CrossAxisAlignment.center,
        children: [
          Text(
            primaryText,
            style: Theme.of(context).textTheme.titleLarge!.copyWith(
                  letterSpacing: 0.5,
                  color: textColor,
                ),
            textAlign: TextAlign.center,
            overflow: bWrapPrimaryText ? null : TextOverflow.ellipsis,
          ),
          if (secondaryText != null)
            Padding(
              padding: const EdgeInsets.only(top: 5),
              child: Text(
                secondaryText!,
                style: Theme.of(context).textTheme.labelMedium!.copyWith(
                      color: textColor,
                    ),
                textAlign: TextAlign.center,
                overflow: bWrapSecondaryText ? null : TextOverflow.ellipsis,
              ),
            ),
          if (emphasisText != null)
            Padding(
              padding: const EdgeInsets.only(top: 5),
              child: Text(
                emphasisText!,
                style: Theme.of(context).textTheme.labelMedium!.copyWith(
                      color: textColor,
                      fontStyle: FontStyle.italic,
                    ),
                textAlign: TextAlign.center,
                overflow: bWrapEmphasisText ? null : TextOverflow.ellipsis,
              ),
            ),
        ],
      ),
    );

    return InkWell(
      onTap: onTap,
      child: Container(
        width: bIsWide ? 230 : 170,
        height: bIsWide ? 90 : 166,
        decoration: BoxDecoration(
          borderRadius: BorderRadius.circular(UnrealTheme.cardCornerRadius),
          color: fillColor,
          border: bIsFilled
              ? null
              : Border.all(
                  color: Theme.of(context).colorScheme.onSurface.withOpacity(0.2),
                  width: 2,
                ),
        ),
        child: bIsWide
            ? Row(
                mainAxisAlignment: MainAxisAlignment.center,
                crossAxisAlignment: CrossAxisAlignment.center,
                children: [
                  const SizedBox(width: 10),
                  unrealIcon,
                  Expanded(child: dataColumn),
                ],
              )
            : Column(
                mainAxisAlignment: MainAxisAlignment.center,
                crossAxisAlignment: CrossAxisAlignment.center,
                children: [
                  unrealIcon,
                  const SizedBox(height: 16),
                  dataColumn,
                ],
              ),
      ),
    );
  }
}

/// Show a modal dialog in the given build [context] that spins while we wait to connect to the engine.
/// If [title] and/or [message] are provided, they replace the default text shown in the dialog. [connectionName] must
/// be provided if [message] is null in order to generate the default message.
/// The returned future completes with a value of true if the user pressed the cancel button.
Future<bool?> showConnectionDialog(
  BuildContext context, {
  String? connectionName,
  String? title,
  String? message,
}) {
  assert(connectionName != null || message != null);

  final route = GenericModalDialogRoute<bool>(
    bResizeToAvoidBottomInset: true,
    // To avoid confusion, only allow dismissal by pressing the Cancel button
    bIsBarrierDismissible: false,
    builder: (context) => ModalDialogCard(
      child: Container(
        width: MediaQuery.of(context).size.width * 0.5,
        padding: EdgeInsets.all(10),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              mainAxisSize: MainAxisSize.min,
              children: [
                AssetIcon(
                  path: 'packages/epic_common/assets/icons/unreal_u_logo.svg',
                  size: 24,
                ),
                SizedBox(width: 16),
                Text(
                  title ?? EpicCommonLocalizations.of(context)!.connectScreenConnectDialogTitle,
                  style: Theme.of(context).textTheme.displayLarge,
                ),
              ],
            ),
            const SizedBox(height: 16),
            Text(
              message ?? EpicCommonLocalizations.of(context)!.connectScreenConnectDialogMessage(connectionName!),
            ),
            const SizedBox(height: 32),
            SizedBox(
              child: const CircularProgressIndicator(strokeWidth: 8.5),
              height: 175,
              width: 175,
            ),
            Row(mainAxisAlignment: MainAxisAlignment.end, children: [
              EpicLozengeButton(
                label: EpicCommonLocalizations.of(context)!.menuButtonCancel,
                color: Theme.of(context).colorScheme.secondary,
                onPressed: () => Navigator.pop(context, true),
              ),
            ]),
          ],
        ),
      ),
    ),
  );

  return Navigator.of(context).push(route);
}

/// Show a modal displaying an error message for a connection attempt.
/// The returned future completes with a value of true if the user wants to try to connect again.
/// If [title] and/or [message] are provided, they replace the default text shown in the dialog. [connectionName] must
/// be provided if [message] is null in order to generate the default message.
Future<bool?> showConnectionErrorDialog(
  BuildContext context, {
  String? connectionName,
  String? title,
  String? message,
}) {
  assert(connectionName != null || message != null);

  final route = GenericModalDialogRoute<bool>(
    bResizeToAvoidBottomInset: true,
    // To avoid confusion, only allow dismissal by pressing the Cancel button
    bIsBarrierDismissible: false,
    builder: (context) => ModalDialogCard(
      child: Container(
        width: MediaQuery.of(context).size.width * 0.5,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            ModalDialogTitle(
              title: title ?? EpicCommonLocalizations.of(context)!.connectScreenFailedDialogTitle,
              iconPath: 'packages/epic_common/assets/icons/alert_triangle_large.svg',
            ),
            ModalDialogSection(
              child: ParsedRichText(
                message ?? EpicCommonLocalizations.of(context)!.connectScreenFailedDialogMessage(connectionName!),
                style: Theme.of(context).textTheme.bodyMedium,
              ),
            ),
            ModalDialogSection(
              child: Row(
                mainAxisAlignment: MainAxisAlignment.end,
                children: [
                  EpicLozengeButton(
                    onPressed: () => Navigator.of(context).pop(false),
                    label: EpicCommonLocalizations.of(context)!.menuButtonCancel,
                    color: Colors.transparent,
                  ),
                  SizedBox(width: 16),
                  EpicLozengeButton(
                    onPressed: () => Navigator.of(context).pop(true),
                    label: EpicCommonLocalizations.of(context)!.menuButtonRetry,
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    ),
  );

  return Navigator.of(context).push(route);
}
