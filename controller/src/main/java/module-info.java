module com.desktoppet {
    requires javafx.controls;
    requires javafx.fxml;
    requires java.desktop;
    requires org.java_websocket;
    requires com.google.gson;
    requires org.slf4j;
    requires ch.qos.logback.classic;
    requires ch.qos.logback.core;
    exports com.desktoppet;
    exports com.desktoppet.core;
    exports com.desktoppet.network;
    exports com.desktoppet.ui;
    opens com.desktoppet to javafx.fxml;
    opens com.desktoppet.ui to javafx.fxml;
    opens com.desktoppet.model to com.google.gson;
    opens com.desktoppet.core.audio to com.google.gson;
}
