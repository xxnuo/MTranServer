
#include <emscripten.h>
#include <stdlib.h>

EM_JS_DEPS(webidl_binder, "$intArrayFromString,$UTF8ToString,$alignMemory,$addOnInit");

extern "C" {

// Define custom allocator functions that we can force export using
// EMSCRIPTEN_KEEPALIVE.  This avoids all webidl users having to add
// malloc/free to -sEXPORTED_FUNCTIONS.
EMSCRIPTEN_KEEPALIVE void webidl_free(void* p) { free(p); }
EMSCRIPTEN_KEEPALIVE void* webidl_malloc(size_t len) { return malloc(len); }


// Interface: Language


const char* EMSCRIPTEN_KEEPALIVE emscripten_bind_Language_getLanguageCode_0(Language* self) {
  return self->getLanguageCode();
}

void EMSCRIPTEN_KEEPALIVE emscripten_bind_Language___destroy___0(Language* self) {
  delete self;
}

// Interface: VoidPtr


void EMSCRIPTEN_KEEPALIVE emscripten_bind_VoidPtr___destroy___0(void** self) {
  delete self;
}

// Interface: LanguageGuess


char EMSCRIPTEN_KEEPALIVE emscripten_bind_LanguageGuess_getPercent_0(LanguageGuess* self) {
  return self->getPercent();
}

const char* EMSCRIPTEN_KEEPALIVE emscripten_bind_LanguageGuess_getLanguageCode_0(LanguageGuess* self) {
  return self->getLanguageCode();
}

void EMSCRIPTEN_KEEPALIVE emscripten_bind_LanguageGuess___destroy___0(LanguageGuess* self) {
  delete self;
}

// Interface: LanguageInfo


LanguageInfo* EMSCRIPTEN_KEEPALIVE emscripten_bind_LanguageInfo_detectLanguageWithLength_3(char* buffer, int bufferLength, bool isPlainText) {
  return LanguageInfo::detectLanguageWithLength(buffer, bufferLength, isPlainText);
}

LanguageInfo* EMSCRIPTEN_KEEPALIVE emscripten_bind_LanguageInfo_detectLanguageWithLength_6(char* buffer, int bufferLength, bool isPlainText, char* tldHint, int encodingHint, char* languageHint) {
  return LanguageInfo::detectLanguageWithLength(buffer, bufferLength, isPlainText, tldHint, encodingHint, languageHint);
}

LanguageInfo* EMSCRIPTEN_KEEPALIVE emscripten_bind_LanguageInfo_detectLanguage_2(char* buffer, bool isPlainText) {
  return LanguageInfo::detectLanguage(buffer, isPlainText);
}

LanguageInfo* EMSCRIPTEN_KEEPALIVE emscripten_bind_LanguageInfo_detectLanguage_5(char* buffer, bool isPlainText, char* tldHint, int encodingHint, char* languageHint) {
  return LanguageInfo::detectLanguage(buffer, isPlainText, tldHint, encodingHint, languageHint);
}

bool EMSCRIPTEN_KEEPALIVE emscripten_bind_LanguageInfo_getIsReliable_0(LanguageInfo* self) {
  return self->getIsReliable();
}

const char* EMSCRIPTEN_KEEPALIVE emscripten_bind_LanguageInfo_getLanguageCode_0(LanguageInfo* self) {
  return self->getLanguageCode();
}

EM_JS(void, array_bounds_check_error, (size_t idx, size_t size), {
  throw 'Array index ' + idx + ' out of bounds: [0,' + size + ')';
});

static void array_bounds_check(size_t array_size, size_t array_idx) {
  if (array_idx < 0 || array_idx >= array_size) {
    array_bounds_check_error(array_idx, array_size);
  }
}

const LanguageGuess* EMSCRIPTEN_KEEPALIVE emscripten_bind_LanguageInfo_get_languages_1(LanguageInfo* self, int arg0) {
  return (array_bounds_check(sizeof(self->languages) / sizeof(self->languages[0]), arg0), self->languages[arg0]);
}

void EMSCRIPTEN_KEEPALIVE emscripten_bind_LanguageInfo___destroy___0(LanguageInfo* self) {
  delete self;
}

}

