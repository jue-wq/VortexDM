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
                    Label { anchors.horizontalCenter: parent.horizontalCenter; text: "The desktop UI is an incremental shell; see the phase checklist."; color: "#8B97AD" }
                }
            }
        }
    }
}
