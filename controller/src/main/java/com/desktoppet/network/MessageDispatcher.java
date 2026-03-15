package com.desktoppet.network;

import com.desktoppet.model.Envelope;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.time.Duration;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;

public class MessageDispatcher {

    private static final Logger log = LoggerFactory.getLogger(MessageDispatcher.class);

    private final Map<String, Consumer<Envelope>> eventHandlers = new ConcurrentHashMap<>();
    private final Map<String, CompletableFuture<Envelope>> pendingResponses = new ConcurrentHashMap<>();

    public void registerEventHandler(String action, Consumer<Envelope> handler) {
        eventHandlers.put(action, handler);
    }

    public CompletableFuture<Envelope> expectResponse(String messageId, Duration timeout) {
        CompletableFuture<Envelope> future = new CompletableFuture<>();
        pendingResponses.put(messageId, future);

        future.orTimeout(timeout.toMillis(), TimeUnit.MILLISECONDS)
            .whenComplete((r, t) -> pendingResponses.remove(messageId));

        return future;
    }

    public void dispatch(Envelope msg) {
        if ("response".equals(msg.type())) {
            CompletableFuture<Envelope> pending = pendingResponses.remove(msg.id());
            if (pending != null) {
                pending.complete(msg);
            }
            return;
        }

        if ("event".equals(msg.type())) {
            Consumer<Envelope> handler = eventHandlers.get(msg.action());
            if (handler == null) {
                log.warn("No handler registered for event action: {}", msg.action());
                return;
            }

            try {
                handler.accept(msg);
            } catch (Exception e) {
                log.error("Error in event handler for {}: {}", msg.action(), e.getMessage(), e);
            }
        }
    }
}
