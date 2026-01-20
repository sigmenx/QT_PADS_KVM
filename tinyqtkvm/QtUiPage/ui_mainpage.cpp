#include "ui_mainpage.h"
#include "ui_ui_mainpage.h"
#include "ui_display.h"

#include <QDebug>
#include <QMessageBox>
#include <QSerialPortInfo>
#include <QDir>

// Linux 底层库
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

// ============================================================================
// 静态辅助函数：整合你的格式检查与占用检测逻辑
// ============================================================================

// 1. 检查设备是否有可用的视频格式
static bool isDeviceHasValidFormat(int fd) {
    struct v4l2_fmtdesc fmt;
    memset(&fmt, 0, sizeof(fmt));

    // 尝试常规 Capture
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_ENUM_FMT, &fmt) >= 0) return true;

    // 尝试 Multi-planar (RK3566 ISP可能需要)
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd, VIDIOC_ENUM_FMT, &fmt) >= 0) return true;

    return false;
}

// 2. 检测设备是否被占用
static bool isDeviceBusy(int fd) {
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 1;
    req.memory = V4L2_MEMORY_MMAP;

    // 先尝试常规类型
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        // 如果是无效参数(EINVAL)，可能是因为设备是MPLANE类型，尝试MPLANE
        if (errno == EINVAL) {
            req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
                if (errno == EBUSY) return true; // 确实忙
                return false; // 其他错误
            }
        } else if (errno == EBUSY) {
            return true; // 设备忙
        } else {
            return false; // 其他错误，暂且视为不忙
        }
    }

    // 如果请求成功，说明没被占用，立即释放缓冲区
    req.count = 0;
    // 注意：type 必须和上面请求成功时的一致
    ioctl(fd, VIDIOC_REQBUFS, &req);
    return false;
}

// ==========================================
// 辅助函数
// ==========================================

void ui_mainpage::updateStatusLabel(QLabel *label, const QString &Text, const QString &Color)
{
    label->setText(Text);
    QString sts;
    sts = sts + "color:" + Color + "; font-weight: bold;";
    label->setStyleSheet(sts);
}

// ==========================================
// 构造函数
// ==========================================

ui_mainpage::ui_mainpage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ui_mainpage)
{
    ui->setupUi(this);

    initUI();

    // 初始加载数据
    refreshCameraList();
    refreshSerialList();
}

ui_mainpage::~ui_mainpage()
{
    delete ui;
}

// ==========================================
// 初始化逻辑
// ==========================================

void ui_mainpage::initUI()
{
    // ---------------------------------------------------------
    // 1. 初始化 ElaTableView (Model/View 模式)
    // ---------------------------------------------------------

    // 创建数据模型
    m_cameraModel = new QStandardItemModel(this);

    // 设置表头
    QStringList camHeaders;
    camHeaders << "设备名称" << "系统路径" << "设备状态";
    m_cameraModel->setHorizontalHeaderLabels(camHeaders);

    // 将模型绑定到 ElaTableView
    // 注意：请确保你在 UI 文件中已经将 cameraTable 提升为 ElaTableView
    ui->cameraTable->setModel(m_cameraModel);

    // 隐藏左侧垂直表头
    ui->cameraTable->verticalHeader()->setVisible(false);

    // 设置列宽调整模式 (View 负责显示策略)
    QHeaderView* camHeader = ui->cameraTable->horizontalHeader();
    camHeader->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    camHeader->setSectionResizeMode(1, QHeaderView::Stretch);
    camHeader->setSectionResizeMode(2, QHeaderView::Stretch);

    // 行为设置
    ui->cameraTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->cameraTable->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->cameraTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // ---------------------------------------------------------
    // 2. 初始化 Label 样式 (保持原有逻辑)
    // ---------------------------------------------------------
    updateStatusLabel(ui->labelSerialStatus, "等待操作","gray");
    updateStatusLabel(ui->labelHIDStatus, "等待操作","gray");

}

// ==========================================
// 业务逻辑 - 刷新列表
// ==========================================

void ui_mainpage::refreshCameraList()
{
    // 1. 清空模型
    if (m_cameraModel) {
        m_cameraModel->removeRows(0, m_cameraModel->rowCount());
    }

    QDir devDir("/dev");
    QStringList filters;
    filters << "video*";
    QStringList videoNodes = devDir.entryList(filters, QDir::System);

    int validCount = 0;

    for (const QString &nodeName : videoNodes) {
        // 1. 字符串过滤：排除 video-enc, video-dec
        if (!nodeName.startsWith("video") || nodeName.contains("-")) {
            continue;
        }

        QString fullPath = "/dev/" + nodeName;

        // 2. 打开设备
        // 【关键】必须使用 O_RDWR，否则 REQBUFS 会失败
        int fd = ::open(fullPath.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;

        struct v4l2_capability cap;
        memset(&cap, 0, sizeof(cap));

        // 3. 基础能力验证 (QueryCap)
        if (::ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
            ::close(fd);
            continue;
        }

        bool isCapture = (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
                         (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE);
        bool isStreaming = (cap.capabilities & V4L2_CAP_STREAMING);

        // 4. 综合判断：是捕获设备 + 支持流 + 有有效格式
        if (isCapture && isStreaming && isDeviceHasValidFormat(fd)) {

            // 5. 检测占用状态
            bool isBusy = isDeviceBusy(fd);

            // === 添加到 UI 列表 ===
            QList<QStandardItem*> rowItems;

            // 列 0: 名称
            QString cardName = QString::fromUtf8((char*)cap.card);
            rowItems << new QStandardItem(cardName);

            // 列 1: 路径
            QStandardItem *itemPath = new QStandardItem(fullPath);
            itemPath->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
            rowItems << itemPath;

            // 列 2: 状态 (根据 busy 标志显示不同内容)
            QStandardItem *itemStatus = new QStandardItem("");
            // 只有当“不忙”时，才视为真正可用 (isSuccess = !isBusy)
            itemStatus->setData(!isBusy, Qt::UserRole);
            rowItems << itemStatus;

            m_cameraModel->appendRow(rowItems);

            // 设置状态列 Label
            int newRowIndex = m_cameraModel->rowCount() - 1;
            QModelIndex statusIndex = m_cameraModel->index(newRowIndex, 2);
            QLabel *statusLabel = new QLabel();
            statusLabel->setAlignment(Qt::AlignCenter);

            if (isBusy) {
                statusLabel->setText("占用 (Busy)");
                statusLabel->setStyleSheet("color: red; font-weight: bold; background-color: transparent;");
            } else {
                statusLabel->setText("就绪 (Ready)");
                statusLabel->setStyleSheet("color: #009600; font-weight: bold; background-color: transparent;");
            }
            ui->cameraTable->setIndexWidget(statusIndex, statusLabel);

            validCount++;
            qDebug() << "Found Camera:" << cardName << "Path:" << fullPath << "Busy:" << isBusy;
        }

        // 别忘了关闭文件描述符
        ::close(fd);
    }

    if (validCount == 0) {
        qDebug() << "No valid camera devices found.";
    }
}

void ui_mainpage::refreshSerialList()
{
    ui->comboSerialDevice->clear();

    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : infos) {
        QString itemText = info.portName();
        if(!info.description().isEmpty()) {
            itemText += " : " + info.description();
        }
        ui->comboSerialDevice->addItem(itemText, info.portName());
    }

    bool hasPorts = (ui->comboSerialDevice->count() > 0);
    if (!hasPorts) {
        ui->comboSerialDevice->addItem("未检测到串口");
    }

    ui->comboSerialDevice->setEnabled(hasPorts);
    ui->btnOpenSerialHID->setEnabled(hasPorts);

}

// ==========================================
// 交互逻辑
// ==========================================

void ui_mainpage::on_btnOpenSerialHID_clicked()
{
    QString portName = ui->comboSerialDevice->currentData().toString();
    if (portName.isEmpty()) {
        QMessageBox::warning(this, "警告", "无效的串口设备！");
        return;
    }

    // 测试 串口 与 CH9329
    CH9329Driver tempDriver;
    // 1. 先拼接完整路径
    QString fullPortPath = "/dev/" + portName;
    QString SERSTAT= "启用失败";QString SERCOL= "red";
    QString HIDSTAT= "通信失败";QString HIDCOL= "red";

    // 2. 转换为const char*（本地编码）并传入init
    if(tempDriver.init(fullPortPath.toLocal8Bit().constData(), 9600)){
        SERSTAT="启用成功";SERCOL= "green";
        if(tempDriver.checkConnection()) {
            HIDSTAT="通信成功";HIDCOL= "green";
        }
    }
    updateStatusLabel(ui->labelSerialStatus, SERSTAT,SERCOL);
    updateStatusLabel(ui->labelHIDStatus, HIDSTAT,HIDCOL);
}

void ui_mainpage::on_btnOpen_clicked()
{
    // 1. 获取当前选中的索引 (QModelIndex)
    QModelIndex currentIndex = ui->cameraTable->currentIndex();

    if (!currentIndex.isValid()) {
        QMessageBox::warning(this, "提示", "请先选择一个视频设备！");
        return;
    }

    // 获取当前行号
    int row = currentIndex.row();

    // 2. 从模型中获取数据
    // 获取第 1 列 (路径) 的文本
    QString camPath = m_cameraModel->item(row, 1)->text();

    // 获取第 2 列 (状态) 的 UserRole 数据
    bool isCamReady = m_cameraModel->item(row, 2)->data(Qt::UserRole).toBool();

    if (!isCamReady) {
        QMessageBox::critical(this, "错误", "该设备已被占用或无法访问！");
        return;
    }

    //打开 Display 的逻辑
    qDebug() << "Launching Display -> Cam:" << camPath;

    ui_display *disp = new ui_display(camPath);
    disp->setAttribute(Qt::WA_DeleteOnClose);


    // 核心步骤：连接信号
    // 当 display 发出 windowClosed 信号时，主界面执行刷新槽函数
    connect(disp, &ui_display::windowClosed, this, [this](){
        // 使用 SingleShot 延迟 500ms 刷新，给系统释放文件句柄的时间
        QTimer::singleShot(500, this, &ui_mainpage::on_btnRefresh_clicked);
    });

    disp->show();

    refreshCameraList();
}

void ui_mainpage::on_btnRefresh_clicked()
{
    usleep(200000);
    refreshCameraList();
    refreshSerialList();

    // 刷新时重置状态 Label
    updateStatusLabel(ui->labelSerialStatus, "等待操作","gray");
    updateStatusLabel(ui->labelHIDStatus, "等待操作","gray");

}



