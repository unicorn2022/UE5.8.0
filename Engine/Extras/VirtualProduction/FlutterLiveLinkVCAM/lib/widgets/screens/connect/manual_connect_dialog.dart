// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:io';

import 'package:epic_common/constants.dart';
import 'package:epic_common/localizations.dart';
import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';
import 'package:flutter_gen/gen_l10n/app_localizations.dart';

import '../../../util/net_utilities.dart';

/// String which will trigger demo mode when entered as the IP address.
const String _demoModeString = 'demo.mode';

/// Show the form for manually connecting to the engine.
/// Returned future completes with the data to use for connection, or null if the user cancels.
Future<EngineConnectionData?> showManualConnectForm(BuildContext context) {
  final route = GenericModalDialogRoute<EngineConnectionData>(
    bResizeToAvoidBottomInset: true,
    builder: (_) => _ManualConnectForm(),
  );

  return Navigator.of(context, rootNavigator: true).push(route);
}

/// Manual connection form for manually connecting to an instance of UE.
class _ManualConnectForm extends StatefulWidget {
  const _ManualConnectForm({Key? key}) : super(key: key);

  @override
  State<_ManualConnectForm> createState() => _ManualConnectFormState();
}

class _ManualConnectFormState extends State<_ManualConnectForm> {
  /// Global key for managing form state & validating form input.
  final _formKey = GlobalKey<FormState>();

  /// Input controller for IP address on the form.
  final _ipTextController = TextEditingController(text: '');

  /// Input controller for PORT on the form.
  final _portTextController = TextEditingController(text: '80');

  @override
  Widget build(BuildContext context) {
    return SafeArea(
      child: ModalDialogCard(
        child: Form(
          key: _formKey,
          child: SizedBox(
            width: 300,
            child: IntrinsicHeight(
              child: Column(
                children: [
                  ModalDialogTitle(title: EpicCommonLocalizations.of(context)!.connectScreenConnectionFormTitle),
                  Expanded(
                    child: EpicScrollView(
                      child: Column(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          ModalDialogSection(
                            child: TextFormField(
                              cursorWidth: 1,
                              style: Theme.of(context).textTheme.bodyMedium,
                              // This mode shows both numbers and letters, which is used for the demo mode string
                              keyboardType: TextInputType.visiblePassword,
                              decoration: collapsedInputDecoration.copyWith(
                                hintText: AppLocalizations.of(context)!.hintIpAddress,
                              ),
                              controller: _ipTextController,
                              validator: _validateAddress,
                            ),
                          ),
                          ModalDialogSection(
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                TextFormField(
                                  cursorWidth: 1,
                                  style: Theme.of(context).textTheme.bodyMedium,
                                  keyboardType: const TextInputType.numberWithOptions(
                                    decimal: false,
                                    signed: false,
                                  ),
                                  decoration: collapsedInputDecoration.copyWith(
                                    hintText: AppLocalizations.of(context)!.hintNetworkPort,
                                  ),
                                  controller: _portTextController,
                                  validator: _validatePort,
                                ),
                                Padding(
                                  padding: EdgeInsets.only(
                                    left: 8,
                                    right: 8,
                                    top: 8,
                                  ),
                                  child: ParsedRichText(
                                    AppLocalizations.of(context)!.manualConnectPortTip,
                                    style: Theme.of(context).textTheme.labelSmall,
                                  ),
                                ),
                              ],
                            ),
                          ),
                          ModalDialogSection(
                            child: Row(
                              mainAxisAlignment: MainAxisAlignment.end,
                              children: [
                                EpicLozengeButton(
                                  label: EpicCommonLocalizations.of(context)!.menuButtonCancel,
                                  color: Colors.transparent,
                                  onPressed: () => Navigator.of(context).pop(),
                                ),
                                EpicLozengeButton(
                                  label: EpicCommonLocalizations.of(context)!.menuButtonOK,
                                  onPressed: _onConnect,
                                ),
                              ],
                            ),
                          ),
                        ],
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }

  /// Callback for when the connect button is clicked or pressed.
  void _onConnect() {
    if (_formKey.currentState?.validate() != true) {
      return;
    }

    final EngineConnectionData connectionData;

    if (_ipTextController.text == _demoModeString) {
      connectionData = EngineConnectionData.forDemoMode();
    } else {
      connectionData = EngineConnectionData(
        name: EpicCommonLocalizations.of(context)!.connectScreenManualConnectionLabel,
        pixelStreamingAddress: InternetAddress(_ipTextController.text),
        pixelStreamingPort: int.parse(_portTextController.text),
      );
    }

    Navigator.of(context).pop(connectionData);
  }

  /// String validation for validating IpAddresses.
  String? _validateAddress(String? text) {
    if (text == _demoModeString) {
      return null;
    }

    if (text == null) {
      return EpicCommonLocalizations.of(context)!.formErrorInvalidIPAddress;
    }

    try {
      InternetAddress(text);
    } catch (e) {
      return EpicCommonLocalizations.of(context)!.formErrorInvalidIPAddress;
    }
    return null;
  }

  /// String validation for validating ports.
  String? _validatePort(String? text) {
    if (text == null) {
      return EpicCommonLocalizations.of(context)!.formErrorInvalidNetworkPort;
    }

    try {
      final port = int.parse(text);
      if (port > 65535) {
        return EpicCommonLocalizations.of(context)!.formErrorInvalidNetworkPort;
      }
    } catch (e) {
      return EpicCommonLocalizations.of(context)!.formErrorInvalidNetworkPort;
    }
    return null;
  }
}
