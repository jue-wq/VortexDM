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
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            id: downloadUrl
                            Layout.fillWidth: true
                            placeholderText: "Paste an HTTP or HTTPS download URL"
                        }
                        Button {
                            text: "Add download"
                            onClicked: {
                                if (downloadsModel.addDownload(downloadUrl.text)) downloadUrl.clear()
                            }
                        }
                    }
                    Label { text: "Downloads · " + downloadsModel.count; color: "#E6EDF5"; font.pixelSize: 19; font.bold: true }
                    ListView {
                        id: downloadList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: downloadsModel
                        clip: true
                        spacing: 8
                        delegate: Rectangle {
                            required property string filename
                            required property string status
                            required property string url
                            width: downloadList.width
                            height: 72
                            radius: 8
                            color: "#181D28"
                            border.color: "#242B3B"
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Label { text: filename; color: "#E6EDF5"; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Label { text: url; color: "#8B97AD"; font.pixelSize: 11; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                }
                                Label {
                                    text: status
                                    color: status === "Completed" ? "#22C55E" : status === "Error" ? "#EF4444" : "#4F8CFF"
                                }
                            }
                        }
                        Label {
                            anchors.centerIn: parent
                            visible: downloadsModel.count === 0
                            text: "No downloads yet"
                            color: "#8B97AD"
                        }
                    }
                    Loader {
                        active: Qt.platform.os === "android"
                        Layout.fillWidth: true
                        sourceComponent: Component {
                            Rectangle {
                                Layout.fillWidth: true
                                height: 174
                                radius: 10
                                color: "#181D28"
                                border.color: "#2B3548"
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 14
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
