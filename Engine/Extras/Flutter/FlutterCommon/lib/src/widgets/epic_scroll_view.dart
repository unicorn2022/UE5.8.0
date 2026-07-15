// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:ui';

import 'package:flutter/material.dart';

import '../../theme.dart';
import '../../utilities/preloadable_shader.dart';
import 'epic_list_view.dart';

/// A custom-styled scrolling view which includes a padded vertical scrollbar and an optional edge fade effect only when
/// scrolling is necessary.
class EpicScrollView extends StatefulWidget {
  const EpicScrollView({
    Key? key,
    required this.child,
    this.bFadeEdges = false,
  }) : super(key: key);

  /// The contents to scroll if necessary.
  final Widget child;

  /// If true, show a fade effect at the overflowing edges of the scroll container.
  final bool bFadeEdges;

  static Future preloadShaders() => _EpicScrollViewShader.load();
  static void unloadShaders() => _EpicScrollViewShader.unload();

  @override
  State<EpicScrollView> createState() => _EpicScrollViewState();
}

class _EpicScrollViewState extends State<EpicScrollView> with SingleTickerProviderStateMixin {
  final ScrollController _controller = ScrollController();
  late final _EpicScrollViewShader shader;

  /// Whether to display the scrollbar
  bool _bScrollbarVisible = false;

  /// The maximum scroll extent when the scrollbar was last enabled
  double? _lastMaxScrollExtent;

  @override
  void initState() {
    super.initState();
    shader = _EpicScrollViewShader(
      scrollController: _controller,
      vsync: this,
    );
  }

  @override
  void dispose() {
    shader.dispose();
    _controller.dispose();

    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    Widget inner = NotificationListener<ScrollMetricsNotification>(
      onNotification: _onScrollMetricsNotification,
      child: EpicScrollBar(
        controller: _controller,
        thumbColor: UnrealColors.gray31,
        child: ScrollConfiguration(
          behavior: ScrollConfiguration.of(context).copyWith(scrollbars: false),
          child: SingleChildScrollView(
            padding: EdgeInsets.only(right: _bScrollbarVisible ? EpicScrollBar.totalWidth : 0),
            controller: _controller,
            child: widget.child,
          ),
        ),
      ),
    );

    if (widget.bFadeEdges) {
      inner = ShaderMask(
        shaderCallback: shader.shaderMaskCallback,
        child: inner,
        blendMode: BlendMode.modulate,
      );
    }

    return MediaQuery.removePadding(
      context: context,
      removeBottom: true,
      removeTop: true,
      child: inner,
    );
  }

  @override
  void reassemble() {
    super.reassemble();

    shader.reload();
  }

  /// If there's enough content to scroll, enable the scrollbar. Otherwise, hide it.
  bool _onScrollMetricsNotification(ScrollMetricsNotification notification) {
    if (!mounted) {
      return false;
    }

    final ScrollMetrics metrics = notification.metrics;
    if (metrics.axisDirection != AxisDirection.down) {
      return false;
    }

    final double lineHeight = DefaultTextStyle.of(context).style.fontSize ?? 14;

    final bool bNewScrollbarVisible =
        (metrics.hasContentDimensions && metrics.minScrollExtent < metrics.maxScrollExtent) ||
            // If there's no scroll extent, but the previous scroll extent was less than one line, we will show the
            // scrollbar anyway. This prevents us from flickering on auto-sized content where adding the scrollbar
            // shrinks it, then removing the scrollbar causes it to grow again.
            ((_lastMaxScrollExtent?.compareTo(lineHeight) ?? 0) < 0);

    if (bNewScrollbarVisible != _bScrollbarVisible) {
      if (bNewScrollbarVisible) {
        _lastMaxScrollExtent = metrics.maxScrollExtent;
      }

      setState(() {
        _bScrollbarVisible = bNewScrollbarVisible;
      });
    }

    return false;
  }
}

/// Indices for each uniform value in the shader used by [_EpicScrollViewShader].
enum _ScrollViewShaderVars {
  uScrollbarPadding,
  uStrength,
  uScrollOffset,
  uMaxScrollOffset,
  uSizeX,
  uSizeY,
}

/// Shader that fades the top and bottom of the scroll view when there's more to scroll in that direction.
class _EpicScrollViewShader extends PreloadableShader {
  _EpicScrollViewShader({
    required this.scrollController,
    required TickerProvider vsync,
  }) : _fadeInController = AnimationController(
          value: 0,
          upperBound: 1,
          duration: const Duration(milliseconds: 100),
          vsync: vsync,
        ) {
    scrollController.addListener(_updateOffset);
    _fadeInController.addListener(_updateStrength);

    // Scroll controller won't be ready immediately, so try updating and fading in the effect after build
    WidgetsBinding.instance.addPostFrameCallback((_) {
      _updateOffset();
      _fadeInEffectIfReady();
    });
  }

  static final PreloadableShaderProgram _program =
      PreloadableShaderProgram('packages/epic_common/assets/shaders/scroll_view_fade.frag');

  /// The scroll controller for the corresponding scroll view.
  final ScrollController scrollController;

  /// The animation controller used to fade in the effect when ready.
  final AnimationController _fadeInController;

  /// Whether this has been disposed.
  bool _bIsDisposed = false;

  @override
  PreloadableShaderProgram get program => _program;

  /// Load or re-load the shader program.
  static Future<FragmentProgram> load() => _program.load();

  /// Unload the shader program.
  static void unload() => _program.unload();

  /// Callback function passed to a [ShaderMask] which updates the bounding rectangle.
  Shader shaderMaskCallback(Rect rect) {
    shader!.setFloat(_ScrollViewShaderVars.uSizeX.index, rect.width);
    shader!.setFloat(_ScrollViewShaderVars.uSizeY.index, rect.height);

    return shader!;
  }

  @override
  void dispose() {
    super.dispose();

    if (_bIsDisposed) {
      return;
    }

    scrollController.removeListener(_updateOffset);
    _fadeInController.dispose();
    _bIsDisposed = true;
  }

  @override
  void initUniforms() {
    shader!.setFloat(_ScrollViewShaderVars.uScrollbarPadding.index, EpicScrollBar.totalWidth);
    _updateStrength();
    _updateOffset();
  }

  /// Fade in the effect if it hasn't already and the scroll controller is ready to be accessed.
  void _fadeInEffectIfReady() {
    if (_bIsDisposed ||
        _fadeInController.isCompleted ||
        _fadeInController.isAnimating ||
        scrollController.positions.isEmpty) {
      return;
    }

    _fadeInController.animateTo(1.0);
  }

  /// Update the effect's strength based on the fade in animation.
  void _updateStrength() {
    shader!.setFloat(_ScrollViewShaderVars.uStrength.index, _fadeInController.value);
  }

  /// Update the shader's properties based on the [scrollController]'s position.
  void _updateOffset() {
    if (scrollController.positions.isEmpty || !scrollController.position.hasContentDimensions) {
      return;
    }

    _fadeInEffectIfReady();

    shader!.setFloat(_ScrollViewShaderVars.uScrollOffset.index, scrollController.offset);
    shader!.setFloat(_ScrollViewShaderVars.uMaxScrollOffset.index, scrollController.position.maxScrollExtent);
  }
}
