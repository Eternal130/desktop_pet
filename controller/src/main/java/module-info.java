module com.desktoppet {
    requires javafx.controls;
    requires javafx.fxml;
    requires java.desktop;
    requires org.java_websocket;
    requires com.google.gson;
    requires org.slf4j;
    exports com.desktoppet;
    exports com.desktoppet.core;
    opens com.desktoppet to javafx.fxml;
    opens com.desktoppet.model to com.google.gson;
}
