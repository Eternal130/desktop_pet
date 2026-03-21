package com.desktoppet.model;

public record VoicePackAction(
    int id,
    String motionPath,
    String audioPath,
    String lipSyncPath,
    String doc,
    long fadeInMs,
    long fadeOutMs
) {}
