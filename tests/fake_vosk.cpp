// SPDX-License-Identifier: AGPL-3.0-or-later
// Lifecycle test double for the Vosk C API: no recognition. A chunk whose first byte is 'E' ends an
// utterance; any other chunk is speech. Changing the grammar while an utterance is being heard is
// recorded as a violation, because the real library stops the program then.
#include <cstring>
#include <string>

namespace {
int model = 0, recognizer = 0;
bool running = false;
int grammarChanges = 0, violations = 0, created = 0;
std::string grammar = "(none)";
const char *const known[] = {"buongiorno", "a", "tutti", "l'incidente", "è", "avvenuto", "nel",
                             "duemilaventitre", "dell'", "anno", "[unk]", "eh", "ehm"};
} // namespace

extern "C" {
const char *fake_grammar() { return grammar.c_str(); }
int fake_grammar_changes() { return grammarChanges; }
int fake_violations() { return violations; }
int fake_recognizers() { return created; }

void vosk_set_log_level(int) {}
void *vosk_model_new(const char *) { return &model; }
void vosk_model_free(void *) {}
int vosk_model_find_word(void *, const char *word) {
    for (int i = 0; i < int(sizeof known / sizeof *known); ++i)
        if (std::strcmp(known[i], word) == 0)
            return i;
    return -1;
}
void *vosk_recognizer_new(void *, float) {
    grammarChanges = violations = 0;
    ++created;
    running = false;
    grammar = "(none)";
    return &recognizer;
}
void *vosk_recognizer_new_grm(void *, float, const char *text) {
    void *result = vosk_recognizer_new(nullptr, 0);
    grammar = text;
    return result;
}
void vosk_recognizer_set_grm(void *, const char *text) {
    violations += running;
    ++grammarChanges;
    grammar = text;
}
void vosk_recognizer_free(void *) {}
int vosk_recognizer_accept_waveform(void *, const char *data, int length) {
    const bool end = length > 0 && data[0] == 'E';
    running = !end;
    return end ? 1 : 0;
}
const char *vosk_recognizer_result(void *) { return R"({"text": "buongiorno a tutti"})"; }
const char *vosk_recognizer_partial_result(void *) { return R"({"partial": "buongiorno"})"; }
}
