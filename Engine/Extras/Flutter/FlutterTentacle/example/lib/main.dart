import 'package:flutter/material.dart';
import 'package:flutter_tentacle/tentacle.dart';
import 'package:epic_common/logging.dart';

void main() {
  Logging.instance.initialize('flutter_tentacle example');

  runApp(const MyApp());
}

class MyApp extends StatefulWidget {
  const MyApp({super.key});

  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  final _deviceManager = TentacleDeviceManager();

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(
          title: const Text('Plugin example app'),
        ),
        body: Center(
          child: StreamBuilder(
            stream: _deviceManager.devicesStream,
            builder: (context, snapshot) => ListView(
              children: [
                for (TentacleDevice device in snapshot.data ?? [])
                  Text(
                    device.info.name,
                  ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
