// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:ui';

import 'package:flutter/material.dart';

/// A static class containing colors used in Unreal's editor.
class UnrealColors {
  static const Color highlightBlue = Color(0xff0070e0);
  static const Color highlightRed = Color(0xffef3535);
  static const Color highlightGreen = Color(0xff8bc24a);
  static const Color highlightTeal = Color(0xff00a78f);
  static const Color highlightPurple = Color(0xff8637ac);
  static const Color highlightYellow = Color(0xfff5cc00);
  static const Color warning = Color(0xffffa640);
  static const Color black = Color(0xff000000);
  static const Color white = Color(0xffffffff);

  // Gray colors are named after the % lightness (as in HSL's L component)
  static const Color gray06 = Color(0xff0f0f0f);
  static const Color gray10 = Color(0xff1a1a1a);
  static const Color gray13 = Color(0xff202020);
  static const Color gray14 = Color(0xff242424);
  static const Color gray18 = Color(0xff2f2f2f);
  static const Color gray22 = Color(0xff383838);
  static const Color gray31 = Color(0xff505050);
  static const Color gray42 = Color(0xff6a6a6a);
  static const Color gray56 = Color(0xff909090);
  static const Color gray75 = Color(0xffc0c0c0);
  static const Color gray90 = Color(0xffe6e6e6);
  static const Color gray94 = Color(0xfff0f0f0);
}

/// A static class containing constants and functions for the standardized Unreal theme used across all Flutter apps.
class UnrealTheme {
  /// Radius of the corners for top-level UI elements.
  static const double outerCornerRadius = 16;

  /// Margin between the cards in a tab.
  static const double cardMargin = 8;

  /// Radius between sections within cards.
  static const double sectionMargin = 4;

  /// Radius of the corners for cards in a tab.
  static const double cardCornerRadius = 8;

  /// Padding for scrollable list views within cards.
  static const EdgeInsetsGeometry cardListViewPadding = EdgeInsets.only(top: 2);

  /// Create an instance of [ThemeData] that can be passed to a [MaterialApp].
  static ThemeData makeThemeData() {
    const textTheme = const TextTheme(
      bodyMedium: TextStyle(
        color: UnrealColors.gray75,
        decorationColor: UnrealColors.gray75,
        fontVariations: [FontVariation('wght', 400)],
        fontSize: 14,
      ),
      displayLarge: TextStyle(
        color: UnrealColors.white,
        decorationColor: UnrealColors.white,
        fontVariations: [FontVariation('wght', 700)],
        fontSize: 14,
      ),
      displayMedium: TextStyle(
        color: UnrealColors.white,
        decorationColor: UnrealColors.white,
        fontVariations: [FontVariation('wght', 400)],
        fontSize: 14,
      ),
      headlineSmall: TextStyle(
        color: UnrealColors.gray75,
        decorationColor: UnrealColors.gray75,
        fontVariations: [FontVariation('wght', 600)],
        letterSpacing: 0.25,
        fontSize: 14,
      ),
      titleLarge: TextStyle(
        color: UnrealColors.gray90,
        decorationColor: UnrealColors.gray90,
        fontVariations: [FontVariation('wght', 700)],
        letterSpacing: 1,
        fontSize: 12,
      ),
      labelMedium: TextStyle(
        color: UnrealColors.gray56,
        decorationColor: UnrealColors.gray56,
        fontVariations: [FontVariation('wght', 400)],
        letterSpacing: 0.5,
        fontSize: 12,
      ),
    );

    const colorScheme = ColorScheme.dark(
      primary: UnrealColors.highlightBlue,
      secondary: Color(0xff575757),
      onPrimary: UnrealColors.white,
      onSecondary: UnrealColors.white,
      background: UnrealColors.gray06,
      surfaceVariant: UnrealColors.gray10,
      surface: UnrealColors.gray14,
      surfaceTint: UnrealColors.gray18,
      onSurface: UnrealColors.gray75,
      shadow: Colors.transparent,
    );

    return ThemeData(
      fontFamily: 'Inter',

      textTheme: textTheme,
      colorScheme: colorScheme,

      // Disable Material "ink" splash effects
      splashFactory: NoSplash.splashFactory,

      scaffoldBackgroundColor: UnrealColors.gray10,
      hoverColor: Colors.transparent,
      highlightColor: Colors.transparent,

      textButtonTheme: TextButtonThemeData(
        style: ButtonStyle(
          overlayColor: MaterialStateProperty.all(Colors.transparent),
        ),
      ),

      checkboxTheme: CheckboxThemeData(
        overlayColor: MaterialStateProperty.all(Colors.transparent),
      ),

      appBarTheme: const AppBarTheme(
        shadowColor: Colors.transparent,
      ),

      tooltipTheme: const TooltipThemeData(
        waitDuration: Duration(milliseconds: 700),
        decoration: BoxDecoration(
          image: DecorationImage(
            image: AssetImage('packages/epic_common/assets/images/tooltip.png'),
            centerSlice: Rect.fromLTRB(4, 4, 60, 60),
          ),
        ),
      ),

      textSelectionTheme: const TextSelectionThemeData(
        cursorColor: UnrealColors.white,
      ),

      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        outlineBorder: BorderSide.none,
        border: OutlineInputBorder(
          borderRadius: BorderRadius.circular(4),
          borderSide: BorderSide.none,
        ),
        enabledBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(4),
          borderSide: BorderSide.none,
        ),
        focusedBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(4),
          borderSide: BorderSide.none,
        ),
        errorBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(4),
          borderSide: BorderSide.none,
        ),
        fillColor: colorScheme.background,
        hoverColor: colorScheme.background,
        contentPadding: const EdgeInsets.symmetric(
          horizontal: 12,
        ),
        hintStyle: textTheme.bodyMedium!.copyWith(color: UnrealColors.gray56),
      ),

      listTileTheme: const ListTileThemeData(
        textColor: UnrealColors.gray75,
        iconColor: UnrealColors.gray75,
        selectedColor: UnrealColors.white,
        selectedTileColor: UnrealColors.highlightBlue,
      ),

      scrollbarTheme: const ScrollbarThemeData(
        thumbColor: MaterialStatePropertyAll(UnrealColors.gray22),
        thickness: MaterialStatePropertyAll(8),
        radius: Radius.circular(4),
        mainAxisMargin: cardCornerRadius,
        crossAxisMargin: cardCornerRadius,
      ),

      cardTheme: CardTheme(
        color: UnrealColors.gray10,
        clipBehavior: Clip.antiAlias,
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(cardCornerRadius)),
        margin: EdgeInsets.zero,
        shadowColor: Colors.transparent,
      ),

      dividerTheme: const DividerThemeData(
        color: UnrealColors.gray22,
      ),
    );
  }
}
