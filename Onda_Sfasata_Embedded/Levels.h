#ifndef LEVELS_H
#define LEVELS_H
#include <Arduino.h>

struct Word {
  const char* word;
  const char* filename;
  const char* trans[5];
  const char* definition;
  const char* info;
};

struct Level {
  int id;
  const char* title;
  Word words[13]; // Supports up to 12 words per level
};

extern Level levels[];
extern const int numLevels;
#endif
