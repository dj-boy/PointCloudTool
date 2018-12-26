//
// Created by Ryan Shen on 2018/11/6.
//
// Visualization Toolkit
#include <vtkRenderWindow.h>

#include <QFileDialog>
#include <QtWidgets/QWidget>
#include <QMessageBox>
#include <QSignalMapper>
#include <iostream>
#include <vector>
#include <QDateTime>
#include "tool/triangulation.h"
#include "tool/projection.h"
#include "tool/resampling.h"
#include "tool/filter.h"
#include "tool/utils.h"
#include "pojo/MyException.h"

#include "main_window.h"

// 初始化静态对象指针
// 点云框选删除需要的静态变量区
static MainWindow *pThis = nullptr; // 静态对象指针
static int selected_cloud_num = 0;
static pcl::PointCloud<pcl::PointXYZ>::Ptr selected_points(new pcl::PointCloud<pcl::PointXYZ>);
static std::vector<int> totalIndices; // 记录被剔除的点

MainWindow::MainWindow(QWidget *parent)
        : QMainWindow(parent) {

    ui.setupUi(this);

    pThis = this;

    // Initialization
    Initial();

    // File
    connect(ui.actionOpen, SIGNAL(triggered()), this, SLOT(OpenFile()));
    connect(ui.actionSave, SIGNAL(triggered()), this, SLOT(SaveFile()));
    connect(ui.actionClear, SIGNAL(triggered()), this, SLOT(Clear()));
    // Exit
    connect(ui.actionExit, SIGNAL(triggered()), this, SLOT(Exit()));

    // View point
    connect(ui.actionMainview, SIGNAL(triggered()), this, SLOT(MainView()));
    connect(ui.actionLeftview, SIGNAL(triggered()), this, SLOT(LeftView()));
    connect(ui.actionTopview, SIGNAL(triggered()), this, SLOT(TopView()));

    // View
    connect(ui.actionPointsViewer, SIGNAL(triggered()), this, SLOT(OpenPointsViewer()));

    /** Algorithm **/
    // Surface Reconstruction
    auto *mapper = new QSignalMapper();
    connect(ui.actionMeshsurface, SIGNAL(triggered()), mapper, SLOT(map()));
    mapper->setMapping(ui.actionMeshsurface, 1);
    connect(mapper, SIGNAL(mapped(int)), this, SLOT(DisplayTriangles(int)), Qt::UniqueConnection);
    connect(ui.actionWireframe, SIGNAL(triggered()), mapper, SLOT(map()));
    mapper->setMapping(ui.actionWireframe, 3);
    connect(mapper, SIGNAL(mapped(int)), this, SLOT(DisplayTriangles(int)), Qt::UniqueConnection);
    // Projection
    connect(ui.actionProjection, SIGNAL(triggered()), this, SLOT(DisplayProjection()));
    // Resample
    connect(ui.actionResample, SIGNAL(triggered()), this, SLOT(GetResample()));
    // filter
    connect(ui.actionStatisticalOutlierRemoval, SIGNAL(triggered()), this, SLOT(FilterStatisticalOutlierRemoval()));

    // remove select points
    connect(ui.actionRemovePoint, SIGNAL(triggered()), this, SLOT(RemoveSelectPoints()));
}

MainWindow::~MainWindow() {

}

void MainWindow::Initial() {
    ui.qvtkWidget_mesh->setVisible(false);

    // 初始化两个VTK Widget
    InitialVtkWidget();
    InitialMeshVtkWidget();

    // Console 区域设置为 NoSelection
    ui.consoleTable->setSelectionMode(QAbstractItemView::NoSelection);

    // 初始化选中点云的计数
    selected_cloud_num = 0;
}

void MainWindow::InitialVtkWidget() {
    cloud.reset(new pcl::PointCloud<pcl::PointXYZ>);
    viewer.reset(new pcl::visualization::PCLVisualizer("point cloud viewer", false));
    viewer->addPointCloud(cloud, "cloud");

    // 注册选取点云的回调函数
    viewer->registerAreaPickingCallback(pp_callback, (void *) &cloud);

    ui.qvtkWidget->SetRenderWindow(viewer->getRenderWindow());
    viewer->setupInteractor(ui.qvtkWidget->GetInteractor(), ui.qvtkWidget->GetRenderWindow());
    ui.qvtkWidget->update();
}

void MainWindow::InitialMeshVtkWidget() {
    viewer_mesh.reset(new pcl::visualization::PCLVisualizer("mesh viewer", false));

    ui.qvtkWidget_mesh->SetRenderWindow(viewer_mesh->getRenderWindow());
    viewer_mesh->setupInteractor(ui.qvtkWidget_mesh->GetInteractor(), ui.qvtkWidget_mesh->GetRenderWindow());
    ui.qvtkWidget_mesh->update();
}

void MainWindow::pp_callback(const pcl::visualization::AreaPickingEvent &event, void *args) {
    /**
     * 使用pThis指针解决回调函数访问非静态成员
     */
    std::vector<int> indices;
    // 清空记录删除点的vector!
    std::vector<int>().swap(totalIndices);
    if (event.getPointsIndices(indices) == -1)
        return;
    for (int i = 0; i < indices.size(); ++i) {
        // push_back 从尾部插入数据
        selected_points->points.push_back(pThis->cloud->points.at(indices[i]));
        totalIndices.push_back(indices[i]);
    }
    pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZ> red(selected_points, 255, 0, 0);

    std::stringstream ss;
    std::string cloudName;
    ss << selected_cloud_num++;
    ss >> cloudName;
    cloudName += "_cloudName";

    pThis->viewer->addPointCloud(selected_points, red, cloudName);
    pThis->viewer->setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 5, cloudName);
    pThis->ui.qvtkWidget->update();
}

// 读取文本和二进制点云数据
void MainWindow::OpenFile() {
    // 打开pcd文件
    QString fileName = QFileDialog::getOpenFileName(this,
                                                    tr("Open PointCloud"), ".",
                                                    tr("Open PCD files(*.pcd)"));

    if (!fileName.isEmpty()) {
        std::string file_name = fileName.toStdString();
        pcl::PCLPointCloud2 cloud2;
        Eigen::Vector4f origin;
        Eigen::Quaternionf orientation;
        int pcd_version;
        int data_type;
        unsigned int data_idx;
        int offset = 0;
        pcl::PCDReader rd;
        rd.readHeader(file_name, cloud2, origin, orientation, pcd_version, data_type, data_idx);

        if (data_type == 0) {
            pcl::io::loadPCDFile(fileName.toStdString(), *cloud);
        } else if (data_type == 2) {
            pcl::PCDReader reader;
            reader.read<pcl::PointXYZ>(fileName.toStdString(), *cloud);
        }
        viewer->removePointCloud("cloud", 0);
        viewer->addPointCloud(cloud, "cloud");
        viewer->resetCamera();
        ui.qvtkWidget->update();

        // operation log
        SaveLog("加载文件" + fileName, "成功");
    } else {
        SaveLog("加载文件" + fileName, "失败");
    }
}

void MainWindow::SaveFile() {
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save point cloud"), "",
                                                    tr("Point cloud data (*.pcd *.vtk)"));
    if (fileName.isEmpty())
        return;
    int return_status;
    if (fileName.endsWith(".pcd", Qt::CaseInsensitive))
        return_status = pcl::io::savePCDFileASCII(fileName.toStdString(), *cloud);
    else if (fileName.endsWith(".vtk", Qt::CaseInsensitive))
        return_status = pcl::io::saveVTKFile(fileName.toStdString(), mesh);
    else {
        fileName.append(".pcd");
        return_status = pcl::io::savePCDFileASCII(fileName.toStdString(), *cloud);
    }

    if (return_status != 0) {
        SaveLog("保存点云/网格", "失败");
        return;
    } else {
        SaveLog("保存点云/网格", "成功，文件名为：" + fileName);
    }
}

void MainWindow::Clear() {
//    viewer->removePointCloud("cloud", 0);
//    viewer_mesh->removePolygonMesh("mesh", 0);

    // 更加彻底的移除
    viewer->removeAllPointClouds();
    viewer->removeAllShapes();
    viewer_mesh->removeAllPointClouds();
    viewer_mesh->removeAllShapes();

    ui.qvtkWidget->update();
    ui.qvtkWidget_mesh->update();

    // operation log
    SaveLog("清空VTK，并重置记录", "");
}

void MainWindow::Exit() {
    if (!(QMessageBox::information(this, tr("提示"), tr("你确定要退出程序吗？请检查所有内容是否保存。"), tr("Yes"), tr("No")))) {
        QApplication *app;
        app->exit(0);
    }
}

// View point setting
void MainWindow::MainView() {
    viewer->setCameraPosition(0, -1, 0, 0.5, 0.5, 0.5, 0, 0, 1);
    ui.qvtkWidget->update();
}

void MainWindow::LeftView() {
    viewer->setCameraPosition(-1, 0, 0, 0, 0, 0, 0, 0, 1);
    ui.qvtkWidget->update();
}

void MainWindow::TopView() {
    viewer->setCameraPosition(0, 0, 1, 0, 0, 0, 0, 1, 0);
    ui.qvtkWidget->update();
}

void MainWindow::DisplayTriangles(int type) {
    viewer_mesh->removePolygonMesh("mesh", 0);
    mesh = GreedyProjection(cloud);
//    mesh = Poisson(cloud);
    viewer_mesh->addPolygonMesh(mesh, "mesh");

    //设置网格模型显示模式
    switch (type) {
        case 1:
            viewer_mesh->setRepresentationToSurfaceForAllActors();
            break; //网格模型以面片形式显示
        case 2:
            viewer_mesh->setRepresentationToPointsForAllActors();
            break; //网格模型以点形式显示，一般不会用到
        case 3:
            viewer_mesh->setRepresentationToWireframeForAllActors();
            break; //网格模型以线框图模式显示
        default:
            break;
    }

    viewer_mesh->resetCamera();
    ui.qvtkWidget_mesh->update();

    // operation log
    SaveLog("重建网格", type == 1 ? "面片模式" : "线框模式");
}

// View
void MainWindow::OpenPointsViewer() {
    ui.qvtkWidget_mesh->setVisible(ui.actionPointsViewer->isChecked());
}

void MainWindow::DisplayProjection() {
    int type = 1;
    pcl::PointCloud<pcl::PointXYZ>::Ptr projectedCloud = GetProjection(cloud, type);

    // use a custom handler
    pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZ> red(projectedCloud, 255, 0, 0);
    // add projectedCloud to current vtk viewer
    viewer->addPointCloud(projectedCloud, red, "Projected Cloud");
    viewer->resetCamera();
    ui.qvtkWidget->update();

    QString fileName = QFileDialog::getSaveFileName(this, tr("Save point cloud"), "",
                                                    tr("Point cloud data (*.pcd)"));
    pcl::PCDWriter writer;
    writer.write(fileName.toStdString(), *projectedCloud, false);

    // operation log
    std::string modelType;
    switch (type) {
        case 1:
            "SACMODEL_PLANE";
            break;
        default:
            modelType = "";
            break;
    }
    SaveLog("点云投影", QString::fromStdString(modelType));
}

void MainWindow::GetResample() {
    pcl::PointCloud<pcl::PointNormal> mls_points = MLSResampling(cloud);
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save point cloud"), "",
                                                    tr("Point cloud data (*.pcd)"));
    pcl::PCDWriter writer;
    writer.write(fileName.toStdString(), mls_points, false);
}

void MainWindow::FilterStatisticalOutlierRemoval() {
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_filtered = StatisticalOutlierRemovalFilter(cloud, false);
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save point cloud"), "",
                                                    tr("Point cloud data (*.pcd)"));
    pcl::PCDWriter writer;
    writer.write(fileName.toStdString(), *cloud_filtered, false);
}

// Remove select points
void MainWindow::RemoveSelectPoints() {
    try {
        std::vector<int> FINALIndices;
        for (int i = 0; i < cloud->size(); ++i) {
            if (!FindInVector(i, totalIndices)) {
                FINALIndices.push_back(i);
            }
        }

        pcl::copyPointCloud(*cloud, FINALIndices, *cloud);
        viewer->removeAllPointClouds();
        viewer->addPointCloud(cloud, "cloud");
        ui.qvtkWidget->update();

        // 清空存储选中点的vector
        selected_points->clear();

        // save log
        SaveLog("去除框选点", "成功");
    }
    catch (MyException &e) {
        SaveLog("处理发生错误", "请重试");
    }
}

// Save operation log
void MainWindow::SaveLog(QString operation, QString note) {
    int rows = ui.consoleTable->rowCount();
    ui.consoleTable->setRowCount(++rows);
    QDateTime now = QDateTime::currentDateTime();
    QString now_str = now.toString("MM-dd hh:mm:ss");
    ui.consoleTable->setItem(rows - 1, 0, new QTableWidgetItem(now_str));
    ui.consoleTable->setItem(rows - 1, 1, new QTableWidgetItem(operation));
    ui.consoleTable->setItem(rows - 1, 2, new QTableWidgetItem(note));

    ui.consoleTable->scrollToBottom();
}
