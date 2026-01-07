#ifndef DRV_CH9329_H
#define DRV_CH9329_H

#include <QObject>
#include <QSerialPort>
#include <vector>
#include <QString>

class CH9329Driver : public QObject
{
    Q_OBJECT
public:
    explicit CH9329Driver(QObject *parent = nullptr);
    ~CH9329Driver();

    // 初始化串口：portName 例如 "COM3" 或 "ttyUSB0"
    bool init(const QString &portName, int baudRate);

    // 关闭串口
    void closeDevice();

    // 检查连接（同步阻塞检查，仅初始化时调用）
    bool checkConnection();

    // --- 鼠标业务功能函数 ---
    void sendMouseAbs(int x, int y, uint8_t buttons, int8_t wheel);
    void sendMouseRel(int xRel, int yRel, uint8_t buttons, int8_t wheel);
    void clickMouse(uint8_t button);

    // --- 键盘业务功能函数 ---
    void sendKbPacket(uint8_t modifiers, uint8_t key);

private:
    // 持有一个Q串口
    QSerialPort *m_serial;
    // 内部发包函数
    void sendPacket(uint8_t command, const std::vector<uint8_t> &data);
};

#endif // DRV_CH9329_H
