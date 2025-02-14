import QtQuick 2.15
import QtQuick.Layouts 1.12
import QtQuick.Controls 2.15

ColumnLayout {
    id: lcsModInfoEdit
    width: parent.width
    property alias image: fieldImage.text

    function clear() {
        fieldName.text = ""
        fieldAuthor.text = ""
        fieldVersion.text = ""
        fieldDescription.text = ""
        fieldImage.text = ""
    }

    function getInfoData() {
        if (fieldName.text === "") {
            window.logUserError("Edit mod", "Mod name can't be empty!")
            return
        }
        let name = fieldName.text == "" ? "UNKNOWN" : fieldName.text
        let author = fieldAuthor.text == "" ? "UNKNOWN" : fieldAuthor.text
        let version = fieldVersion.text == "" ? "1.0" : fieldVersion.text
        let description = fieldDescription.text
        let info = {
            "Name": name,
            "Author": author,
            "Version": version,
            "Description": description
        }
        return info
    }

    function setInfoData(info) {
        fieldName.text = info["Name"]
        fieldAuthor.text = info["Author"]
        fieldVersion.text = info["Version"]
        fieldDescription.text = info["Description"]
    }

    RowLayout {
        Layout.fillWidth: true
        Label {
            text: qsTr("Name: ")
        }
        TextField {
            id: fieldName
            Layout.fillWidth: true
            placeholderText: "Name"
            selectByMouse: true
            validator: RegularExpressionValidator {
                regularExpression: window.validName
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Label {
            text: qsTr("Author: ")
        }
        TextField {
            id: fieldAuthor
            Layout.fillWidth: true
            placeholderText: "Author"
            selectByMouse: true
            validator: RegularExpressionValidator {
                regularExpression: window.validName
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Label {
            text: qsTr("Version: ")
        }
        TextField {
            id: fieldVersion
            Layout.fillWidth: true
            placeholderText: "0.0.0"
            selectByMouse: true
            validator: RegularExpressionValidator {
                regularExpression: /([0-9]{1,3})(\.[0-9]{1,3}){0,3}/
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Label {
            text: qsTr("Description: ")
        }
        TextField {
            id: fieldDescription
            placeholderText: "Description"
            Layout.fillWidth: true
            selectByMouse: true
        }
    }

    Image {
        id: imagePreview
        Layout.fillHeight: true
        Layout.fillWidth: true
        fillMode: Image.PreserveAspectFit
        source: LCSUtils.toFile(fieldImage.text)
    }

    RowLayout {
        Layout.fillWidth: true
        Button {
            text: qsTr("Select Image")
            onClicked: dialogImage.open()
        }
        TextField {
            id: fieldImage
            Layout.fillWidth: true
            placeholderText: ""
            readOnly: true
            selectByMouse: true
        }
        ToolButton {
            text: "\uf00d"
            font.family: "FontAwesome"
            onClicked: fieldImage.text = ""
        }
    }

    LCSDialogNewModImage {
        id: dialogImage
        onAccepted: fieldImage.text = LCSUtils.fromFile(file)
    }
}
