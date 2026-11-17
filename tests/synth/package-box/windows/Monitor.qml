import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/* The synthetic core's own window (issue 610), drawn by Chimera from this
 * file with the core as its view model: `core` is the core's window "monitor"
 * (its properties, its commands through core.run, its lists through
 * core.rows), `texts` the package's texts in the language in use. It shows
 * the registers, the instruction running and the first bytes of RAM, and
 * holds the frame at an instruction until Step or Continue. */
Item {
	id: root
	implicitWidth: 460
	implicitHeight: 380

	ColumnLayout {
		anchors.fill: parent
		anchors.margins: 10
		spacing: 6

		Label {
			objectName: "state"
			text: core.held ? texts["window.monitor.held"] : texts["window.monitor.running"]
			font.bold: true
		}
		Label { objectName: "pc"; text: "PC " + core.pc }
		Label { objectName: "registers"; text: core.registers; font.family: "monospace" }
		RowLayout {
			spacing: 6
			Label { text: texts["window.monitor.stop_at"] }
			TextField {
				objectName: "stopAt"
				text: String(core.stopAt)
				implicitWidth: 80
				onEditingFinished: core.stopAt = parseInt(text)
			}
			Button {
				objectName: "step"
				text: texts["window.monitor.step"]
				enabled: core.held
				onClicked: core.run("step")
			}
			Button {
				objectName: "continue"
				text: texts["window.monitor.continue"]
				enabled: core.held
				onClicked: core.run("continue")
			}
		}
		ListView {
			objectName: "ram"
			Layout.fillWidth: true
			Layout.fillHeight: true
			clip: true
			model: core.rows("ram")
			delegate: Label {
				required property string address
				required property string value
				text: address + ": " + value
				font.family: "monospace"
			}
		}
	}
}
