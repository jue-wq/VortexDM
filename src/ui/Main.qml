import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ApplicationWindow {
    visible: true
    width: 1100
    height: 720
    title: "VortexDM"
    color: "#0E1117"
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            height: 58
            color: "#131720"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                Label { text: "VortexDM"; color: "#E6EDF5"; font.pixelSize: 20; font.bold: true }
                Item { Layout.fillWidth: true }
                Label { text: "Core preview"; color: "#8B97AD" }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Rectangle {
                Layout.preferredWidth: 220
                Layout.fillHeight: true
                color: "#131720"
                Column {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 16
                    Repeater {
                        model: ["All downloads", "Unfinished", "Finished", "Categories", "Queues", "Settings"]
                        delegate: Label { required property string modelData; text: modelData; color: "#E6EDF5"; font.pixelSize: 14 }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#0E1117"
                Column {
                    anchors.centerIn: parent
                    spacing: 8
                    Label { anchors.horizontalCenter: parent.horizontalCenter; text: "Your downloads will appear here"; color: "#E6EDF5"; font.pixelSize: 20 }
                    Label { anchors.horizontalCenter: parent.horizontalCenter; text: "The download workspace is being implemented in phases."; color: "#8B97AD" }
                    Loader {
                        active: Qt.platform.os === "android"
                        sourceComponent: Component {
                            Rectangle {
                                width: 520
                                height: 190
                                radius: 10
                                color: "#181D28"
                                border.color: "#2B3548"
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 16
                                    Label { text: "Android Browser Bridge"; color: "#E6EDF5"; font.bold: true; font.pixelSize: 16 }
                                    Label { text: bridgeServer.running ? "Listening on 127.0.0.1:" + bridgeServer.port : "Loopback server unavailable"; color: bridgeServer.running ? "#22C55E" : "#EF4444" }
                                    Label { text: "One-time API key (copy into Firefox add-on settings)"; color: "#8B97AD" }
                                    TextField { Layout.fillWidth: true; readOnly: true; selectByMouse: true; text: bridgeServer.apiKey; color: "#E6EDF5" }
                                    Text {
                                        text: '<a href="https://addons.mozilla.org/android/addon/ublock-origin/">Install original uBlock Origin for Firefox Android</a>'
                                        textFormat: Text.RichText
                                        onLinkActivated: Qt.openUrlExternally(link)
                                        color: "#4F8CFF"
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
