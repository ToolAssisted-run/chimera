// Fusion's spin box with its value as a plain number, "-1000": with the
// locale's thousands separator ("-1,000") a digit typed over could not be
// deleted, since what was left ("-,000") was refused as no number, and the
// comma was in the way of every edit. While typing, a minus and digits are
// taken; a box left empty or with a minus alone keeps its value. A box that
// shows its number its own way (Display's tenths) sets these itself.
import QtQuick
import QtQuick.Controls.Fusion as Fusion

Fusion.SpinBox {
    id: control
    textFromValue: (v, locale) => String(v)
    valueFromText: (text, locale) => { const n = parseInt(text, 10); return isNaN(n) ? control.value : n }
    validator: RegularExpressionValidator { regularExpression: /-?[0-9]*/ }
}
