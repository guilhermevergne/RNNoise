/* Copyright (c) 2018 Gregor Richards
 * Copyright (c) 2017 Mozilla */
/*
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <math.h>
#include <stdio.h>
#include "rnnoise.h"
#include <stdlib.h>
#include <sys/types.h>
#include "rnnoise.h"

#define FRAME_SIZE 480

int main(int argc, char **argv) {
  int i, ci;
  int first = 1;
  int channels;
  float x[FRAME_SIZE];
  short *tmp;
  RNNModel *model = NULL;
  DenoiseState **sts;
  float max_attenuation;
  if (argc < 3) {
    fprintf(stderr, "usage: %s <channels> <max attenuation dB> [model file]\n", argv[0]);
    return 1;
  }

  channels = atoi(argv[1]);
  if (channels < 1) channels = 1;
  max_attenuation = pow(10, -atof(argv[2])/10);

  if (argc >= 4) {
    FILE *model_file = fopen(argv[3], "r");
    if (!model_file) {
      perror(argv[3]);
      return 1;
    }
    model = rnnoise_model_from_file(model_file);
    fprintf(stderr, "\n\n\n%p\n\n\n", model);
    if (!model) {
      perror(argv[3]);
      return 1;
    }
    fclose(model_file);
  }

  sts = malloc(channels * sizeof(DenoiseState *));
  if (!sts) {
    perror("malloc");
    return 1;
  }
  tmp = malloc(channels * FRAME_SIZE * sizeof(short));
  if (!tmp) {
      perror("malloc");
      return 1;
  }
  for (i = 0; i < channels; i++) {
    sts[i] = rnnoise_create(model);
    rnnoise_set_param(sts[i], RNNOISE_PARAM_MAX_ATTENUATION, max_attenuation);
  }

  while (1) {
    fread(tmp, sizeof(short), channels * FRAME_SIZE, stdin);
    if (feof(stdin)) break;

    for (ci = 0; ci < channels; ci++) {
        for (i=0;i<FRAME_SIZE;i++) x[i] = tmp[i*channels+ci];
        rnnoise_process_frame(sts[ci], x, x);
        for (i=0;i<FRAME_SIZE;i++) tmp[i*channels+ci] = x[i];
    }

    if (!first) fwrite(tmp, sizeof(short), channels * FRAME_SIZE, stdout);
    first = 0;
  }

  for (i = 0; i < channels; i++)
    rnnoise_destroy(sts[i]);
  free(tmp);
  free(sts);
  if (model)
    rnnoise_model_free(model);
  return 0;
}
