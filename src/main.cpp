/**
 * BA Example 
 * In this program, we read two images for feature matching. The camera motion and the position of the feature points are then calculated based on the features obtained by the matching. This is a typical Bundle Adjustment, we optimize it with g2o.
 */

#include "ros/ros.h"
// for std
#include <iostream>
// for opencv 
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/features2d/features2d.hpp>
#include <boost/concept_check.hpp>
// for g2o
#include <g2o/core/sparse_optimizer.h>
#include <g2o/core/block_solver.h>
#include <g2o/core/robust_kernel.h>
#include <g2o/core/robust_kernel_impl.h>
#include <g2o/core/optimization_algorithm_levenberg.h>
#include <g2o/solvers/cholmod/linear_solver_cholmod.h>
#include <g2o/types/slam3d/se3quat.h>
#include <g2o/types/sba/types_six_dof_expmap.h>

using namespace cv;
using namespace std;

// Find the corresponding point in the two images, pixel coordinate system
// Enter: img1, img2 two images
// output: points1, points2, 2D points corresponding to the two groups
int findCorrespondingPoints( const cv::Mat& img1, const cv::Mat& img2, vector<cv::Point2f>& points1, vector<cv::Point2f>& points2 );

// camera internal reference
double cx = 325.5;
double cy = 253.5;
double fx = 518.0;
double fy = 519.0;

int main( int argc, char** argv )
{
    // Call format: command [first picture] [second picture]
    if (argc != 3)
    {
        cout<<"Usage: ba_example img1, img2"<<endl;
        exit(1);
    }
    
    // read the image
    cv::Mat img1 = cv::imread( argv[1] ); 
    cv::Mat img2 = cv::imread( argv[2] ); 
    
    // find the corresponding point
    vector<cv::Point2f> pts1, pts2;
    if ( findCorrespondingPoints( img1, img2, pts1, pts2 ) == false )
    {
        cout<<" matches are not enough!"<<endl;
        return 0;
    }
    cout<<" found the corresponding feature point of the "<<pts1.size()<<" group."<<endl;
    // Construct a graph in g2o
    // Construct the solver first
    
    
   g2o::SparseOptimizer optimizer;
   optimizer.setVerbose(false);
   std::unique_ptr<g2o::BlockSolver_6_3::LinearSolverType> linearSolver;
   linearSolver = g2o::make_unique<g2o::LinearSolverCholmod<g2o::BlockSolver_6_3::PoseMatrixType>>();
   g2o::OptimizationAlgorithmLevenberg* solver = new g2o::OptimizationAlgorithmLevenberg(
   g2o::make_unique<g2o::BlockSolver_6_3>(std::move(linearSolver))
  );
  optimizer.setAlgorithm(solver);




    // add node
    // two pose nodes
    for ( int i=0; i<2; i++ )
    {
        g2o::VertexSE3Expmap* v = new g2o::VertexSE3Expmap();
        v->setId(i);
        if ( i == 0)
            v->setFixed( true ); // The first point is fixed to zero
        // The default value is Pose because we don't know any information.
        v-> setEstimate (g2o :: SE3Quat ());
        optimizer.addVertex( v );
    }
    // nodes with many feature points
    // subject to the first frame
    for ( size_t i=0; i<pts1.size(); i++ )
    {
        g2o::VertexSBAPointXYZ* v = new g2o::VertexSBAPointXYZ();
        v->setId( 2 + i );
        // Since the depth is unknown, you can only set the depth to 1.
        double z = 1;
        double x = ( pts1[i].x - cx ) * z / fx; 
        double y = ( pts1[i].y - cy ) * z / fy; 
        v->setMarginalized(true);
        v-> setEstimate (Eigen :: Vector3d (x, y, z));
        optimizer.addVertex( v );
    }
    
    // Prepare camera parameters
    g2o::CameraParameters* camera = new g2o::CameraParameters( fx, Eigen::Vector2d(cx, cy), 0 );
    camera->setId(0);
    optimizer.addParameter( camera );
    
    // Prepare the side
    // first frame
    vector<g2o::EdgeProjectXYZ2UV*> edges;
    for ( size_t i=0; i<pts1.size(); i++ )
    {
        g2o::EdgeProjectXYZ2UV*  edge = new g2o::EdgeProjectXYZ2UV();
        edge->setVertex( 0, dynamic_cast<g2o::VertexSBAPointXYZ*>   (optimizer.vertex(i+2)) );
        edge->setVertex( 1, dynamic_cast<g2o::VertexSE3Expmap*>     (optimizer.vertex(0)) );
        edge->setMeasurement( Eigen::Vector2d(pts1[i].x, pts1[i].y ) );
        edge->setInformation( Eigen::Matrix2d::Identity() );
        edge->setParameterId(0, 0);
        // kernel function
        edge->setRobustKernel( new g2o::RobustKernelHuber() );
        optimizer.addEdge( edge );
        edges.push_back(edge);
    }
    // second frame
    for ( size_t i=0; i<pts2.size(); i++ )
    {
        g2o::EdgeProjectXYZ2UV*  edge = new g2o::EdgeProjectXYZ2UV();
        edge->setVertex( 0, dynamic_cast<g2o::VertexSBAPointXYZ*>   (optimizer.vertex(i+2)) );
        edge->setVertex( 1, dynamic_cast<g2o::VertexSE3Expmap*>     (optimizer.vertex(1)) );
        edge->setMeasurement( Eigen::Vector2d(pts2[i].x, pts2[i].y ) );
        edge->setInformation( Eigen::Matrix2d::Identity() );
        edge->setParameterId(0,0);
        // kernel function
        edge->setRobustKernel( new g2o::RobustKernelHuber() );
        optimizer.addEdge( edge );
        edges.push_back(edge);
    }
    
    cout<<"Start optimization"<<endl;
    optimizer.setVerbose (true);
    optimizer.initializeOptimization();
    optimizer.optimize(10);
    cout<<"optimized"<<endl;
    
    // We are more concerned about the transformation matrix between two frames
    g2o::VertexSE3Expmap* v = dynamic_cast<g2o::VertexSE3Expmap*>( optimizer.vertex(1) );
    Eigen::Isometry3d pose = v->estimate();
    cout<<"Pose="<<endl<<pose.matrix()<<endl;
    
    // and the location of all feature points
    for ( size_t i=0; i<pts1.size(); i++ )
    {
        g2o::VertexSBAPointXYZ* v = dynamic_cast<g2o::VertexSBAPointXYZ*> (optimizer.vertex(i+2));
        cout<<"vertex id "<<i+2<<", pos = ";
        Eigen::Vector3d pos = v->estimate();
        cout<<pos(0)<<","<<pos(1)<<","<<pos(2)<<endl;
    }
    
    // Estimate the number of inliers
    int inliers = 0;
    for ( auto e:edges )
    {
        e->computeError();
        // chi2 is error*\Omega*error. If this number is large, the value of this side is very different from other edges.
        if ( e->chi2() > 1 )
        {
            cout<<"error = "<<e->chi2()<<endl;
        }
        else 
        {
            inliers ++;
        }
    }
    
    cout<<"inliers in total points: "<<inliers<<"/"<<pts1.size()+pts2.size()<<endl;
    optimizer.save("ba.g2o");
    return 0;
}


int     findCorrespondingPoints( const cv::Mat& img1, const cv::Mat& img2, vector<cv::Point2f>& points1, vector<cv::Point2f>& points2 )
{
    Ptr<ORB> orb = ORB::create();
    vector<cv::KeyPoint> kp1, kp2;
    cv::Mat desp1, desp2;
    orb->detectAndCompute( img1, cv::Mat(), kp1, desp1 );
    orb->detectAndCompute( img2, cv::Mat(), kp2, desp2 );
    cout<<" found "<<kp1.size()<<" and "<<kp2.size()<<" feature points"<<endl;
    
    cv::Ptr<cv::DescriptorMatcher>  matcher = cv::DescriptorMatcher::create( "BruteForce-Hamming");
    
    double knn_match_ratio=0.8;
    vector< vector<cv::DMatch> > matches_knn;
    matcher->knnMatch( desp1, desp2, matches_knn, 2 );
    vector< cv::DMatch > matches;
    for ( size_t i=0; i<matches_knn.size(); i++ )
    {
        if (matches_knn[i][0].distance < knn_match_ratio * matches_knn[i][1].distance )
            matches.push_back( matches_knn[i][0] );
    }
    
    if (matches.size() <= 20) //There are too few matching points
        return false;
    
    for ( auto m:matches )
    {
        points1.push_back( kp1[m.queryIdx].pt );
        points2.push_back( kp2[m.trainIdx].pt );
    }
    cv::Mat res;
    const std::vector< char > ce;
    int i;
    i = 0;
    drawMatches(img1 ,kp1 , img2 ,kp2 , matches, res, Scalar(255, 0, 0), Scalar(255, 0, 0), ce,i);
    imshow( "Display window", res );
    //waitKey(0);  
    return true;
}
