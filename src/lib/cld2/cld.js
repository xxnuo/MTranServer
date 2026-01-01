
// Bindings utilities

/** @suppress {duplicate} (TODO: avoid emitting this multiple times, it is redundant) */
function WrapperObject() {
}
WrapperObject.prototype = Object.create(WrapperObject.prototype);
WrapperObject.prototype.constructor = WrapperObject;
WrapperObject.prototype.__class__ = WrapperObject;
WrapperObject.__cache__ = {};
Module['WrapperObject'] = WrapperObject;

/** @suppress {duplicate} (TODO: avoid emitting this multiple times, it is redundant)
    @param {*=} __class__ */
function getCache(__class__) {
  return (__class__ || WrapperObject).__cache__;
}
Module['getCache'] = getCache;

/** @suppress {duplicate} (TODO: avoid emitting this multiple times, it is redundant)
    @param {*=} __class__ */
function wrapPointer(ptr, __class__) {
  var cache = getCache(__class__);
  var ret = cache[ptr];
  if (ret) return ret;
  ret = Object.create((__class__ || WrapperObject).prototype);
  ret.ptr = ptr;
  return cache[ptr] = ret;
}
Module['wrapPointer'] = wrapPointer;

/** @suppress {duplicate} (TODO: avoid emitting this multiple times, it is redundant) */
function castObject(obj, __class__) {
  return wrapPointer(obj.ptr, __class__);
}
Module['castObject'] = castObject;

Module['NULL'] = wrapPointer(0);

/** @suppress {duplicate} (TODO: avoid emitting this multiple times, it is redundant) */
function destroy(obj) {
  if (!obj['__destroy__']) throw 'Error: Cannot destroy object. (Did you create it yourself?)';
  obj['__destroy__']();
  // Remove from cache, so the object can be GC'd and refs added onto it released
  delete getCache(obj.__class__)[obj.ptr];
}
Module['destroy'] = destroy;

/** @suppress {duplicate} (TODO: avoid emitting this multiple times, it is redundant) */
function compare(obj1, obj2) {
  return obj1.ptr === obj2.ptr;
}
Module['compare'] = compare;

/** @suppress {duplicate} (TODO: avoid emitting this multiple times, it is redundant) */
function getPointer(obj) {
  return obj.ptr;
}
Module['getPointer'] = getPointer;

/** @suppress {duplicate} (TODO: avoid emitting this multiple times, it is redundant) */
function getClass(obj) {
  return obj.__class__;
}
Module['getClass'] = getClass;

// Converts big (string or array) values into a C-style storage, in temporary space

/** @suppress {duplicate} (TODO: avoid emitting this multiple times, it is redundant) */
var ensureCache = {
  buffer: 0,  // the main buffer of temporary storage
  size: 0,   // the size of buffer
  pos: 0,    // the next free offset in buffer
  temps: [], // extra allocations
  needed: 0, // the total size we need next time

  prepare() {
    if (ensureCache.needed) {
      // clear the temps
      for (var i = 0; i < ensureCache.temps.length; i++) {
        Module['_webidl_free'](ensureCache.temps[i]);
      }
      ensureCache.temps.length = 0;
      // prepare to allocate a bigger buffer
      Module['_webidl_free'](ensureCache.buffer);
      ensureCache.buffer = 0;
      ensureCache.size += ensureCache.needed;
      // clean up
      ensureCache.needed = 0;
    }
    if (!ensureCache.buffer) { // happens first time, or when we need to grow
      ensureCache.size += 128; // heuristic, avoid many small grow events
      ensureCache.buffer = Module['_webidl_malloc'](ensureCache.size);
      assert(ensureCache.buffer);
    }
    ensureCache.pos = 0;
  },
  alloc(array, view) {
    assert(ensureCache.buffer);
    var bytes = view.BYTES_PER_ELEMENT;
    var len = array.length * bytes;
    len = alignMemory(len, 8); // keep things aligned to 8 byte boundaries
    var ret;
    if (ensureCache.pos + len >= ensureCache.size) {
      // we failed to allocate in the buffer, ensureCache time around :(
      assert(len > 0); // null terminator, at least
      ensureCache.needed += len;
      ret = Module['_webidl_malloc'](len);
      ensureCache.temps.push(ret);
    } else {
      // we can allocate in the buffer
      ret = ensureCache.buffer + ensureCache.pos;
      ensureCache.pos += len;
    }
    return ret;
  },
};

/** @suppress {duplicate} (TODO: avoid emitting this multiple times, it is redundant) */
function ensureString(value) {
  if (typeof value === 'string') {
    var intArray = intArrayFromString(value);
    var offset = ensureCache.alloc(intArray, HEAP8);
    for (var i = 0; i < intArray.length; i++) {
      HEAP8[offset + i] = intArray[i];
    }
    return offset;
  }
  return value;
}

/** @suppress {duplicate} (TODO: avoid emitting this multiple times, it is redundant) */
function ensureInt8(value) {
  if (typeof value === 'object') {
    var offset = ensureCache.alloc(value, HEAP8);
    for (var i = 0; i < value.length; i++) {
      HEAP8[offset + i] = value[i];
    }
    return offset;
  }
  return value;
}

/** @suppress {duplicate} (TODO: avoid emitting this multiple times, it is redundant) */
function ensureInt16(value) {
  if (typeof value === 'object') {
    var offset = ensureCache.alloc(value, HEAP16);
    var heapOffset = offset / 2;
    for (var i = 0; i < value.length; i++) {
      HEAP16[heapOffset + i] = value[i];
    }
    return offset;
  }
  return value;
}

/** @suppress {duplicate} (TODO: avoid emitting this multiple times, it is redundant) */
function ensureInt32(value) {
  if (typeof value === 'object') {
    var offset = ensureCache.alloc(value, HEAP32);
    var heapOffset = offset / 4;
    for (var i = 0; i < value.length; i++) {
      HEAP32[heapOffset + i] = value[i];
    }
    return offset;
  }
  return value;
}

/** @suppress {duplicate} (TODO: avoid emitting this multiple times, it is redundant) */
function ensureFloat32(value) {
  if (typeof value === 'object') {
    var offset = ensureCache.alloc(value, HEAPF32);
    var heapOffset = offset / 4;
    for (var i = 0; i < value.length; i++) {
      HEAPF32[heapOffset + i] = value[i];
    }
    return offset;
  }
  return value;
}

/** @suppress {duplicate} (TODO: avoid emitting this multiple times, it is redundant) */
function ensureFloat64(value) {
  if (typeof value === 'object') {
    var offset = ensureCache.alloc(value, HEAPF64);
    var heapOffset = offset / 8;
    for (var i = 0; i < value.length; i++) {
      HEAPF64[heapOffset + i] = value[i];
    }
    return offset;
  }
  return value;
}

// Interface: Language

/** @suppress {undefinedVars, duplicate} @this{Object} */
function Language() { throw "cannot construct a Language, no constructor in IDL" }
Language.prototype = Object.create(WrapperObject.prototype);
Language.prototype.constructor = Language;
Language.prototype.__class__ = Language;
Language.__cache__ = {};
Module['Language'] = Language;
/** @suppress {undefinedVars, duplicate} @this{Object} */
Language.prototype['getLanguageCode'] = Language.prototype.getLanguageCode = function() {
  var self = this.ptr;
  return UTF8ToString(_emscripten_bind_Language_getLanguageCode_0(self));
};


/** @suppress {undefinedVars, duplicate} @this{Object} */
Language.prototype['__destroy__'] = Language.prototype.__destroy__ = function() {
  var self = this.ptr;
  _emscripten_bind_Language___destroy___0(self);
};

// Interface: VoidPtr

/** @suppress {undefinedVars, duplicate} @this{Object} */
function VoidPtr() { throw "cannot construct a VoidPtr, no constructor in IDL" }
VoidPtr.prototype = Object.create(WrapperObject.prototype);
VoidPtr.prototype.constructor = VoidPtr;
VoidPtr.prototype.__class__ = VoidPtr;
VoidPtr.__cache__ = {};
Module['VoidPtr'] = VoidPtr;

/** @suppress {undefinedVars, duplicate} @this{Object} */
VoidPtr.prototype['__destroy__'] = VoidPtr.prototype.__destroy__ = function() {
  var self = this.ptr;
  _emscripten_bind_VoidPtr___destroy___0(self);
};

// Interface: LanguageGuess

/** @suppress {undefinedVars, duplicate} @this{Object} */
function LanguageGuess() { throw "cannot construct a LanguageGuess, no constructor in IDL" }
LanguageGuess.prototype = Object.create(Language.prototype);
LanguageGuess.prototype.constructor = LanguageGuess;
LanguageGuess.prototype.__class__ = LanguageGuess;
LanguageGuess.__cache__ = {};
Module['LanguageGuess'] = LanguageGuess;
/** @suppress {undefinedVars, duplicate} @this{Object} */
LanguageGuess.prototype['getPercent'] = LanguageGuess.prototype.getPercent = function() {
  var self = this.ptr;
  return _emscripten_bind_LanguageGuess_getPercent_0(self);
};

/** @suppress {undefinedVars, duplicate} @this{Object} */
LanguageGuess.prototype['getLanguageCode'] = LanguageGuess.prototype.getLanguageCode = function() {
  var self = this.ptr;
  return UTF8ToString(_emscripten_bind_LanguageGuess_getLanguageCode_0(self));
};


/** @suppress {undefinedVars, duplicate} @this{Object} */
LanguageGuess.prototype['__destroy__'] = LanguageGuess.prototype.__destroy__ = function() {
  var self = this.ptr;
  _emscripten_bind_LanguageGuess___destroy___0(self);
};

// Interface: LanguageInfo

/** @suppress {undefinedVars, duplicate} @this{Object} */
function LanguageInfo() { throw "cannot construct a LanguageInfo, no constructor in IDL" }
LanguageInfo.prototype = Object.create(Language.prototype);
LanguageInfo.prototype.constructor = LanguageInfo;
LanguageInfo.prototype.__class__ = LanguageInfo;
LanguageInfo.__cache__ = {};
Module['LanguageInfo'] = LanguageInfo;
/** @suppress {undefinedVars, duplicate} @this{Object} */
LanguageInfo.prototype['detectLanguage'] = LanguageInfo.prototype.detectLanguage = function(buffer, isPlainText, tldHint, encodingHint, languageHint) {
  ensureCache.prepare();
  if (buffer && typeof buffer === 'object') buffer = buffer.ptr;
  else buffer = ensureString(buffer);
  if (isPlainText && typeof isPlainText === 'object') isPlainText = isPlainText.ptr;
  if (tldHint && typeof tldHint === 'object') tldHint = tldHint.ptr;
  else tldHint = ensureString(tldHint);
  if (encodingHint && typeof encodingHint === 'object') encodingHint = encodingHint.ptr;
  if (languageHint && typeof languageHint === 'object') languageHint = languageHint.ptr;
  else languageHint = ensureString(languageHint);
  if (tldHint === undefined) { return wrapPointer(_emscripten_bind_LanguageInfo_detectLanguage_2(buffer, isPlainText), LanguageInfo) }
  if (encodingHint === undefined) { return wrapPointer(_emscripten_bind_LanguageInfo_detectLanguage_3(buffer, isPlainText, tldHint), LanguageInfo) }
  if (languageHint === undefined) { return wrapPointer(_emscripten_bind_LanguageInfo_detectLanguage_4(buffer, isPlainText, tldHint, encodingHint), LanguageInfo) }
  return wrapPointer(_emscripten_bind_LanguageInfo_detectLanguage_5(buffer, isPlainText, tldHint, encodingHint, languageHint), LanguageInfo);
};

/** @suppress {undefinedVars, duplicate} @this{Object} */
LanguageInfo.prototype['getIsReliable'] = LanguageInfo.prototype.getIsReliable = function() {
  var self = this.ptr;
  return !!(_emscripten_bind_LanguageInfo_getIsReliable_0(self));
};

/** @suppress {undefinedVars, duplicate} @this{Object} */
LanguageInfo.prototype['getLanguageCode'] = LanguageInfo.prototype.getLanguageCode = function() {
  var self = this.ptr;
  return UTF8ToString(_emscripten_bind_LanguageInfo_getLanguageCode_0(self));
};

/** @suppress {undefinedVars, duplicate} @this{Object} */
LanguageInfo.prototype['get_languages'] = LanguageInfo.prototype.get_languages = function(arg0) {
  var self = this.ptr;
  if (arg0 && typeof arg0 === 'object') arg0 = arg0.ptr;
  return wrapPointer(_emscripten_bind_LanguageInfo_get_languages_1(self, arg0), LanguageGuess);
};

/** @suppress {checkTypes} */
Object.defineProperty(LanguageInfo.prototype, 'languages', { get: LanguageInfo.prototype.get_languages });

/** @suppress {undefinedVars, duplicate} @this{Object} */
LanguageInfo.prototype['__destroy__'] = LanguageInfo.prototype.__destroy__ = function() {
  var self = this.ptr;
  _emscripten_bind_LanguageInfo___destroy___0(self);
};
