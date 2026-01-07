#include "drv_ch9329.h"
#include <QDebug>
#include <QThread>
#include <unistd.h>

// CH9329 协议常量
const uint8_t CH9329_HEAD_0 = 0x57;
const uint8_t CH9329_HEAD_1 = 0xAB;
const uint8_t CH9329_ADDR_DEFAULT = 0x00;

// 指令集
const uint8_t CMD_GET_INFO = 0x01;
const uint8_t CMD_SEND_KB_GENERAL_DATA = 0x02;
const uint8_t CMD_SEND_MS_REL_DATA = 0x05;
const uint8_t CMD_SEND_MS_ABS_DATA = 0x04;

CH9329Driver::CH9329Driver(QObject *parent)
    : QObject(parent), m_serial(new QSerialPort(this))
{
    // QSerialPort 作为子对象，随父对象自动析构，无需手动 delete
}

CH9329Driver::~CH9329Driver()
{
    closeDevice();
}

bool CH9329Driver::init(const QString &portName, int baudRate)
{
    closeDevice(); //以此确保状态重置

    m_serial->setPortName(portName);
    m_serial->setBaudRate(baudRate);

    // CH9329 默认配置：8数据位，无校验，1停止位，无流控
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    if (m_serial->open(QIODevice::ReadWrite)) {
        // 可选：设置内部缓冲区大小，防止大量数据堆积
        // m_serial->setReadBufferSize(4096);
        return true;
    } else {
        qDebug() << "Serial Open Failed:" << m_serial->errorString();
        return false;
    }
}

void CH9329Driver::closeDevice()
{
    if (m_serial->isOpen()) {
        m_serial->close();
    }
}

// 同步检查函数，仅在初始化连接时使用
bool CH9329Driver::checkConnection()
{
    if (!m_serial->isOpen()) return false;
    // 清空缓冲区
    m_serial->clear();
    std::vector<uint8_t> emptyData;
    sendPacket(CMD_GET_INFO, emptyData);
    // 必须等待数据写入完成
    if (!m_serial->waitForBytesWritten(100)) return false;
    // 等待设备回复 (超时设为 300ms)
    if (m_serial->waitForReadyRead(300)) {
        QByteArray response = m_serial->readAll();
        // 简单校验：长度 > 0 且 帧头正确
        if (response.size() > 0 && (uint8_t)response[0] == CH9329_HEAD_0) {
            return true;
        }
    }
    return false;
}

// === 鼠标逻辑 ===
//绝对坐标
void CH9329Driver::sendMouseAbs(int x, int y, uint8_t buttons, int8_t wheel) {
    if (x < 1) x = 1;
    if (x > 4095) x = 4095;
    if (y < 1) y = 1;
    if (y > 4095) y = 4095;

    std::vector<uint8_t> data;
    data.push_back(0x02);
    data.push_back(buttons);
    data.push_back((uint8_t)(x & 0xFF));
    data.push_back((uint8_t)((x >> 8) & 0xFF));
    data.push_back((uint8_t)(y & 0xFF));
    data.push_back((uint8_t)((y >> 8) & 0xFF));
    data.push_back((uint8_t)((char)wheel));

    //qDebug() << "[CH9329] Abs Send: X=" << x << " Y=" << y << " Btn=" << buttons;
    sendPacket(CMD_SEND_MS_ABS_DATA, data);
}

//相对坐标
void CH9329Driver::sendMouseRel(int xRel, int yRel, uint8_t buttons, int8_t wheel) {
    if (xRel > 127) xRel = 127;
    if (xRel < -128) xRel = -128;
    if (yRel > 127) yRel = 127;
    if (yRel < -128) yRel = -128;

    std::vector<uint8_t> data;
    data.push_back(0x01);
    data.push_back(buttons);
    data.push_back((uint8_t)((char)xRel));
    data.push_back((uint8_t)((char)yRel));
    data.push_back((uint8_t)((char)wheel)); // 滚轮字节
    //data.push_back(0x00);
    sendPacket(CMD_SEND_MS_REL_DATA, data);
}

//点击事件
void CH9329Driver::clickMouse(uint8_t button) {
    sendMouseRel(0, 0, button, 0);
    usleep(50000); // 50ms 点击间隔
    sendMouseRel(0, 0, 0, 0);
}

// === 键盘值发送 ===
void CH9329Driver::sendKbPacket(uint8_t modifiers, uint8_t key) {
    std::vector<uint8_t> data(8, 0x00);
    data[0] = modifiers;
    data[1] = 0x00; // 保留位
    data[2] = key;
    // data[3]~[7] 默认为0
    sendPacket(CMD_SEND_KB_GENERAL_DATA, data);
}
// === 底层发送 ===
void CH9329Driver::sendPacket(uint8_t command, const std::vector<uint8_t> &data)
{
    if (!m_serial || !m_serial->isOpen()) return;

    // 1. 组包
    uint8_t len = data.size();
    uint8_t sum = 0;

    std::vector<uint8_t> packet;
    packet.reserve(len + 7); // 预分配内存优化性能

    packet.push_back(CH9329_HEAD_0);
    packet.push_back(CH9329_HEAD_1);
    packet.push_back(CH9329_ADDR_DEFAULT);
    packet.push_back(command);
    packet.push_back(len);

    sum += CH9329_HEAD_0;
    sum += CH9329_HEAD_1;
    sum += CH9329_ADDR_DEFAULT;
    sum += command;
    sum += len;

    for (uint8_t b : data) {
        packet.push_back(b);
        sum += b;
    }

    packet.push_back(sum);

    // 2. 发送 (非阻塞，写入缓冲区即返回，不会卡顿 UI)
    m_serial->write(reinterpret_cast<const char*>(packet.data()), packet.size());
    // 注意：在此处不需要 waitForBytesWritten，让 Qt 事件循环去处理发送，

}
