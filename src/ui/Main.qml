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
                visible: Qt.platform.os !== "android"
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
                    ColumnLayout {
                        Layout.fillWidth: true
                        ComboBox {
                            id: queueSelector
                            Layout.fillWidth: true
                            model: downloadsModel.queues
                            textRole: "name"
                            valueRole: "id"
                        }
                        TextField {
                            id: downloadUrl
                            Layout.fillWidth: true
                            placeholderText: "Paste an HTTP or HTTPS download URL"
                        }
                        Button {
                            Layout.fillWidth: true
                            text: "Add download"
                            onClicked: {
                                if (downloadsModel.addDownload(downloadUrl.text, queueSelector.currentValue)) downloadUrl.clear()
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label { text: "Downloads · " + downloadsModel.count; color: "#E6EDF5"; font.pixelSize: 19; font.bold: true }
                        RowLayout {
                            TextField { id: newQueueName; Layout.fillWidth: true; placeholderText: "New queue name" }
                            Button { text: "Create queue"; onClicked: { if (downloadsModel.createQueue(newQueueName.text)) newQueueName.clear() } }
                        }
                    }
                    ListView {
                        id: downloadList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: downloadsModel
                        clip: true
                        spacing: 8
                        delegate: Rectangle {
                            required property string taskId
                            required property string filename
                            required property string status
                            required property string url
                            required property double totalBytes
                            required property double downloadedBytes
                            width: downloadList.width
                            height: 82
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
                                    ProgressBar {
                                        Layout.fillWidth: true
                                        implicitHeight: 4
                                        from: 0
                                        to: 1
                                        value: totalBytes > 0 ? Math.min(1, downloadedBytes / totalBytes) : status === "Completed" ? 1 : 0
                                    }
                                }
                                ColumnLayout {
                                    spacing: 4
                                    Label {
                                        text: status
                                        color: status === "Completed" ? "#22C55E" : status === "Error" ? "#EF4444" : "#4F8CFF"
                                    }
                                    RowLayout {
                                        spacing: 4
                                        Button { visible: status === "Downloading" || status === "Queued"; text: "Pause"; onClicked: downloadsModel.controlTask(taskId,"pause") }
                                        Button { visible: status === "Paused"; text: "Resume"; onClicked: downloadsModel.controlTask(taskId,"resume") }
                                        Button { visible: status !== "Completed" && status !== "Cancelled"; text: "Cancel"; onClicked: downloadsModel.controlTask(taskId,"cancel") }
                                    }
                                }
                            }
                        }
                    }
                    Label {
                        visible: downloadsModel.count === 0
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: "No downloads yet"
                        color: "#8B97AD"
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        height: Qt.platform.os === "android" ? 180 : 118
                        radius: 10
                        color: "#181D28"
                        border.color: "#2B3548"
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            Label { text: "Media extraction · yt-dlp"; color: "#E6EDF5"; font.bold: true }
                            ColumnLayout {
                                TextField { id: ytDlpBinary; Layout.fillWidth: true; placeholderText: "Absolute yt-dlp executable path" }
                                TextField { id: ytDlpPython; Layout.fillWidth: true; placeholderText: "Optional Python path (with yt_dlp installed)" }
                                Button { Layout.fillWidth: true; text: "Save profile"; onClicked: downloadsModel.configureYtDlp(ytDlpBinary.text, ytDlpPython.text) }
                            }
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
