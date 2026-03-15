package com.desktoppet.network;

import ch.qos.logback.classic.Level;
import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import com.desktoppet.model.Envelope;
import com.google.gson.JsonObject;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.slf4j.LoggerFactory;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

import static org.junit.jupiter.api.Assertions.*;

class MessageDispatcherTest {

    private MessageDispatcher dispatcher;
    private Logger logger;
    private ListAppender<ILoggingEvent> listAppender;

    @BeforeEach
    void setUp() {
        dispatcher = new MessageDispatcher();
        logger = (Logger) LoggerFactory.getLogger(MessageDispatcher.class);
        listAppender = new ListAppender<>();
        listAppender.start();
        logger.addAppender(listAppender);
    }

    @AfterEach
    void tearDown() {
        logger.detachAppender(listAppender);
        listAppender.stop();
    }

    @Test
    void registerEventHandler_calledOnMatchingAction() {
        AtomicReference<Envelope> received = new AtomicReference<>();
        dispatcher.registerEventHandler("model_loaded", received::set);

        JsonObject payload = new JsonObject();
        payload.addProperty("model", "Hiyori");
        Envelope event = Protocol.createEvent("model_loaded", payload);

        dispatcher.dispatch(event);

        assertNotNull(received.get());
        assertEquals(event, received.get());
    }

    @Test
    void registerMultipleHandlers_eachRoutedCorrectly() {
        AtomicInteger hitCount = new AtomicInteger();
        AtomicInteger dragEndCount = new AtomicInteger();

        dispatcher.registerEventHandler("hit", e -> hitCount.incrementAndGet());
        dispatcher.registerEventHandler("drag_end", e -> dragEndCount.incrementAndGet());

        dispatcher.dispatch(Protocol.createEvent("hit", new JsonObject()));
        dispatcher.dispatch(Protocol.createEvent("drag_end", new JsonObject()));
        dispatcher.dispatch(Protocol.createEvent("hit", new JsonObject()));

        assertEquals(2, hitCount.get());
        assertEquals(1, dragEndCount.get());
    }

    @Test
    void unregisteredAction_doesNotCrash() {
        Envelope event = Protocol.createEvent("unknown_action", new JsonObject());

        assertDoesNotThrow(() -> dispatcher.dispatch(event));

        boolean warned = listAppender.list.stream()
            .anyMatch(log -> log.getLevel() == Level.WARN
                && log.getFormattedMessage().contains("No handler registered for event action: unknown_action"));
        assertTrue(warned);
    }

    @Test
    void handlerThrowsException_doesNotCrash() {
        dispatcher.registerEventHandler("model_loaded", e -> {
            throw new RuntimeException("boom");
        });

        Envelope event = Protocol.createEvent("model_loaded", new JsonObject());

        assertDoesNotThrow(() -> dispatcher.dispatch(event));

        boolean errored = listAppender.list.stream()
            .anyMatch(log -> log.getLevel() == Level.ERROR
                && log.getFormattedMessage().contains("Error in event handler for model_loaded: boom"));
        assertTrue(errored);
    }

    @Test
    void expectResponse_completedOnMatchingId() throws Exception {
        CompletableFuture<Envelope> future = dispatcher.expectResponse("msg-123", Duration.ofSeconds(1));
        Envelope response = Protocol.createResponse("msg-123", "load_model", true, 0, "");

        dispatcher.dispatch(response);

        Envelope completed = future.get(1, TimeUnit.SECONDS);
        assertEquals(response, completed);
    }

    @Test
    void expectResponse_timeout_completesExceptionally() {
        CompletableFuture<Envelope> future = dispatcher.expectResponse("msg-timeout", Duration.ofMillis(100));

        ExecutionException ex = assertThrows(ExecutionException.class, () -> future.get(1, TimeUnit.SECONDS));
        assertInstanceOf(TimeoutException.class, ex.getCause());
    }
}
