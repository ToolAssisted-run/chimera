// Fusion's button, which takes the keyboard focus when it is clicked, on a Mac
// too. Qt's buttons on a Mac take it only by Tab, so a number typed in a box
// was not yet the box's value when OK read it: the Display window's User
// prescale was lost (issue 540). Clicked, a button now ends the editing first,
// as on Windows and Linux.
import QtQuick
import QtQuick.Controls.Fusion as Fusion

Fusion.Button {
    focusPolicy: Qt.StrongFocus
}
