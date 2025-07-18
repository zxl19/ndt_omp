#include <iostream>
#include <ros/ros.h>
#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>
#include <pcl/point_cloud.h>
#include <pcl/registration/ndt.h>
#include <pcl/registration/gicp.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/visualization/pcl_visualizer.h>

#include <pclomp/ndt_omp.h>
#include <pclomp/gicp_omp.h>

/**
 * @brief 打印当前进程的内存占用信息
 *
 * @note 该函数依赖于Linux系统的/proc/self/status文件
 * @note VmPeak: 进程虚拟内存使用峰值
 * @note VmSize: 进程虚拟内存使用大小
 * @note VmStk: 进程栈内存使用大小
 * @note VmData: 进程数据段内存使用大小
 * @note VmRSS: 进程常驻内存使用大小
 * @note VmLib: 进程共享库内存使用大小
 * @note VmPTE: 进程页表内存使用大小
 */
void PrintMemoryUsage() {
  std::ifstream status_file("/proc/self/status");
  if(!status_file.is_open()) {
    std::cout << "Failed to open /proc/self/status";
    return;
  }

  std::cout << std::string(30, '=') << std::endl;
  std::cout << std::left << std::setw(15) << "Memory Type" << std::setw(15) << "Size (MB)" << std::endl;
  std::cout << std::string(30, '-') << std::endl;

  std::string line;
  while(std::getline(status_file, line)) {
    if(line.find("VmPeak") != std::string::npos || line.find("VmSize") != std::string::npos || line.find("VmStk") != std::string::npos || line.find("VmData") != std::string::npos || line.find("VmRSS") != std::string::npos || line.find("VmLib") != std::string::npos || line.find("VmPTE") != std::string::npos) {
      std::size_t pos = line.find(":");
      if(pos != std::string::npos) {
        std::string type = line.substr(0, pos);
        std::string size_str = line.substr(pos + 1);
        std::size_t size = std::stoul(size_str);
        double size_MB = size / 1024.0;  // Convert from KB to MB

        std::cout << std::left << std::setw(15) << type << std::setw(15) << std::fixed << std::setprecision(2) << size_MB << std::endl;
      }
    }
  }

  status_file.close();
}

// align point clouds and measure processing time
pcl::PointCloud<pcl::PointXYZ>::Ptr align(pcl::Registration<pcl::PointXYZ, pcl::PointXYZ>::Ptr registration, const std::string& registration_name, const pcl::PointCloud<pcl::PointXYZ>::Ptr& target_cloud, const pcl::PointCloud<pcl::PointXYZ>::Ptr& source_cloud) {
  registration->setInputTarget(target_cloud);
  registration->setInputSource(source_cloud);
  pcl::PointCloud<pcl::PointXYZ>::Ptr aligned(new pcl::PointCloud<pcl::PointXYZ>());

  auto t1 = ros::WallTime::now();
  registration->align(*aligned);
  auto t2 = ros::WallTime::now();
  std::cout << "single: " << (t2 - t1).toSec() * 1000 << " [msec]" << std::endl;

  for(int i = 0; i < 10; i++) {
    registration->align(*aligned);
  }
  auto t3 = ros::WallTime::now();
  std::cout << "10 times: " << (t3 - t2).toSec() * 1000 << " [msec]" << std::endl;
  std::cout << "fitness: " << registration->getFitnessScore() << std::endl << std::endl;

  if(pcl::io::savePCDFileASCII("/home/pcd/" + registration_name + "_source.pcd", *source_cloud)) {
    std::cerr << "failed to save "
              << "/home/pcd/" + registration_name + "_source.pcd" << std::endl;
  }
  if(pcl::io::savePCDFileASCII("/home/pcd/" + registration_name + "_target.pcd", *target_cloud)) {
    std::cerr << "failed to save "
              << "/home/pcd/" + registration_name + "_target.pcd" << std::endl;
  }
  if(pcl::io::savePCDFileASCII("/home/pcd/" + registration_name + "_aligned.pcd", *aligned)) {
    std::cerr << "failed to save "
              << "/home/pcd/" + registration_name + "_aligned.pcd" << std::endl;
  }

  return aligned;
}

int main(int argc, char** argv) {
  if(argc != 3) {
    std::cout << "usage: align target.pcd source.pcd" << std::endl;
    return 0;
  }

  std::string target_pcd = argv[1];
  std::string source_pcd = argv[2];

  pcl::PointCloud<pcl::PointXYZ>::Ptr target_cloud(new pcl::PointCloud<pcl::PointXYZ>());
  pcl::PointCloud<pcl::PointXYZ>::Ptr source_cloud(new pcl::PointCloud<pcl::PointXYZ>());

  if(pcl::io::loadPCDFile(target_pcd, *target_cloud)) {
    std::cerr << "failed to load " << target_pcd << std::endl;
    return 0;
  }
  if(pcl::io::loadPCDFile(source_pcd, *source_cloud)) {
    std::cerr << "failed to load " << source_pcd << std::endl;
    return 0;
  }

  // downsampling
  pcl::PointCloud<pcl::PointXYZ>::Ptr downsampled(new pcl::PointCloud<pcl::PointXYZ>());

  pcl::VoxelGrid<pcl::PointXYZ> voxelgrid;
  voxelgrid.setLeafSize(0.1f, 0.1f, 0.1f);

  voxelgrid.setInputCloud(target_cloud);
  voxelgrid.filter(*downsampled);
  *target_cloud = *downsampled;

  voxelgrid.setInputCloud(source_cloud);
  voxelgrid.filter(*downsampled);
  source_cloud = downsampled;

  ros::Time::init();

  // benchmark
  std::cout << "--- pcl::GICP ---" << std::endl;
  pcl::GeneralizedIterativeClosestPoint<pcl::PointXYZ, pcl::PointXYZ>::Ptr gicp(new pcl::GeneralizedIterativeClosestPoint<pcl::PointXYZ, pcl::PointXYZ>());
  pcl::PointCloud<pcl::PointXYZ>::Ptr aligned = align(gicp, "pcl_gicp", target_cloud, source_cloud);

  std::cout << "--- pclomp::GICP ---" << std::endl;
  pclomp::GeneralizedIterativeClosestPoint<pcl::PointXYZ, pcl::PointXYZ>::Ptr gicp_omp(new pclomp::GeneralizedIterativeClosestPoint<pcl::PointXYZ, pcl::PointXYZ>());
  aligned = align(gicp_omp, "pclomp_gicp", target_cloud, source_cloud);

  std::cout << "--- pcl::NDT ---" << std::endl;
  pcl::NormalDistributionsTransform<pcl::PointXYZ, pcl::PointXYZ>::Ptr ndt(new pcl::NormalDistributionsTransform<pcl::PointXYZ, pcl::PointXYZ>());
  ndt->setResolution(1.0);
  aligned = align(ndt, "pcl_ndt", target_cloud, source_cloud);

  std::vector<int> num_threads = {1, omp_get_max_threads()};
  std::vector<std::pair<std::string, pclomp::NeighborSearchMethod>> search_methods = {{"KDTREE", pclomp::KDTREE}, {"DIRECT7", pclomp::DIRECT7}, {"DIRECT1", pclomp::DIRECT1}};

  pclomp::NormalDistributionsTransform<pcl::PointXYZ, pcl::PointXYZ>::Ptr ndt_omp(new pclomp::NormalDistributionsTransform<pcl::PointXYZ, pcl::PointXYZ>());
  ndt_omp->setResolution(1.0);

  for(int n : num_threads) {
    for(const auto& search_method : search_methods) {
      std::cout << "--- pclomp::NDT (" << search_method.first << ", " << n << " threads) ---" << std::endl;
      ndt_omp->setNumThreads(n);
      ndt_omp->setNeighborhoodSearchMethod(search_method.second);
      aligned = align(ndt_omp, "pclomp_ndt_" + search_method.first + "_" + std::to_string(n) + "_threads", target_cloud, source_cloud);
    }
  }

  // visualization
  pcl::visualization::PCLVisualizer vis("vis");
  pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZ> target_handler(target_cloud, 255.0, 0.0, 0.0);
  pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZ> source_handler(source_cloud, 0.0, 255.0, 0.0);
  pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZ> aligned_handler(aligned, 0.0, 0.0, 255.0);
  vis.addPointCloud(target_cloud, target_handler, "target");
  vis.addPointCloud(source_cloud, source_handler, "source");
  vis.addPointCloud(aligned, aligned_handler, "aligned");
  vis.spin();

  return 0;
}
