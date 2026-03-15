package com.desktoppet.model;

import com.google.gson.JsonObject;

/**
 * WebSocket message envelope. For type="response", success/errorCode/errorMessage
 * are at JSON TOP LEVEL (not inside payload) — matching C++ Protocol.cpp serialization.
 */
public record Envelope(
    String type,            // "command" | "event" | "response"
    String action,
    String id,
    JsonObject payload,
    long timestamp,
    Boolean success,        // null for non-response messages
    Integer errorCode,      // null for non-response; 0 for success
    String errorMessage     // null for non-response; "" for success
) {}
