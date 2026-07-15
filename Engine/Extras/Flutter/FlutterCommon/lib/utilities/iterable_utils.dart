// Copyright Epic Games, Inc. All Rights Reserved.

extension IterableWithSeparate<E> on Iterable<E> {
  /// Creates a new lazy [Iterable] which places the [separator] between every element in the original iterable.
  Iterable<E> separate(E separator) => _SeparateIterable(this, separator);
}

/// Iterable which places a separator element between every element in the source iterable.
class _SeparateIterable<E> extends Iterable<E> {
  _SeparateIterable(this._iterable, this._separator);

  /// The source iterable.
  final Iterable<E> _iterable;

  /// The separator to place between every element.
  final E _separator;

  @override
  Iterator<E> get iterator => _SeparateIterator<E>(_iterable.iterator, _separator);
}

class _SeparateIterator<E> implements Iterator<E> {
  _SeparateIterator(this._iterator, this._separator);

  /// The source iterator.
  final Iterator<E> _iterator;

  /// The separator to place between every element.
  final E _separator;

  /// Whether there are more elements after the current one.
  bool _bHasMore = false;

  /// The number of elements iterated so far.
  /// Starts at an invalid value because [moveNext] must be called on iterators before accessing the current value.
  int _index = -1;

  /// Whether to return a separator for the current index.
  bool get bShouldReturnSeparator => (_index % 2) == 1;

  @override
  E get current => bShouldReturnSeparator ? _separator : _iterator.current;

  @override
  bool moveNext() {
    final bool bResult;
    ++_index;

    if (_index == 0 || bShouldReturnSeparator) {
      // Check if there are more elements after this one.
      // We always check this on the first element because we may not need to return any separators at all.
      _bHasMore = _iterator.moveNext();
      bResult = _bHasMore;
    } else {
      bResult = true;
    }

    return bResult;
  }
}
