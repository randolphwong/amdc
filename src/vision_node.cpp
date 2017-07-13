#include <ros/ros.h>
#include <ros/console.h>

#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "boost/program_options.hpp"

#include <ctime>
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <unordered_set>

extern "C"
{
#include "darknet/darknet.h"
}

using namespace std;
using namespace cv;
namespace po = boost::program_options;

const string video_url = 
"rtsp://192.168.1.10:554/user=admin&password=&channel=1&stream=0.sdp?";

const Scalar green_colour(0, 255, 0);
const int im_w = 256;
const int im_h = 144;
const int sw_wnd_size = 28;
const int sw_step_size = 14;
const int sw_xsteps = 1 + (im_w - sw_wnd_size) / sw_step_size;
const int sw_ysteps = 1 + (im_h - sw_wnd_size) / sw_step_size;

const int num_of_class = 4;
const int debris_class = 1;

string cfg_file;
string weights_file;
string video_file;
string save_video_file;
bool save_video = false;

network net;

void parse_options(int argc, char **argv)
{
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help,h", "vision node")
        ("cfg,c", po::value<string>(), "darknet configuration file")
        ("weights,w", po::value<string>(), "darknet weights file")
        ("file,f", po::value<string>(), "source video file (leave empty to use camera)")
        ("save,s", "save output video");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);    

    if (vm.count("help"))
    {
        cout << desc << endl;
        exit(0);
    }

    if (vm.count("cfg"))
    {
        cfg_file = vm["cfg"].as<string>();
    }
    else
    {
        cerr << "cfg not set\n";
        exit(-1);
    }

    if (vm.count("weights"))
    {
        weights_file = vm["weights"].as<string>();
    }
    else
    {
        cerr << "weights not set\n";
        exit(-1);
    }

    if (vm.count("file"))
    {
        video_file = vm["file"].as<string>();
    }
    else
    {
        video_file = video_url;
    }


    if (vm.count("save"))
    {
        save_video = true;

        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);
        stringstream ss;
        ss << std::put_time(&tm, "obj_det_%d-%m-%Y_%H-%M-%S.mp4");
        save_video_file = ss.str();
    }
}

/**
 * Merge overlapping Rects.
 * \param out   output Rect
 * \param in    input Rect
 */
void merge_bounding_boxes(vector<Rect>& out, vector<Rect>& in, bool correct_order)
{
    Rect merged;
    if (in.size() == 0)
        return;

    bool unchanged = true;

    unordered_set<int> unmerged;
    for (int i = 0; i < in.size(); ++i)
        unmerged.insert(i);

    for (int i = 0; i < in.size(); ++i)
    {
        merged = in[i];
        bool was_merged = false;

        for (int j = i + 1; j < in.size(); ++j)
        {
            Rect intersection = in[j] & merged;
            if (intersection.area() > 0)
            {
                merged = in[j] | merged;
                unmerged.erase(i);
                unmerged.erase(j);
                was_merged = true;
            }
        }

        if (was_merged)
        {
            unchanged = false;
            out.push_back(merged);
        }
    }

    for (auto &i : unmerged)
        out.push_back(in[i]);

    if (unchanged == false)
    {
        in.clear();
        merge_bounding_boxes(in, out, !correct_order);
    }
    else if (correct_order == false)
    {
        in.clear();
        for (auto &bbox : out)
            in.push_back(bbox);
    }
}

// code adapted from darknet
image Mat_to_image(const Mat &mat)
{
    image im;
    int h, w, c, step;
    unsigned char *data;

    h = mat.rows;
    w = mat.cols;
    c = mat.channels();
    step = mat.step;
    data = (unsigned char *)mat.data;

    im = make_image(w, h, c);

    for(int k = 0; k < c; ++k)
        for(int i = 0; i < h; ++i)
            for(int j = 0; j < w; ++j)
                im.data[(c - k - 1)*w*h + i*w + j] = data[i*step + j*c + k]/255.;

    return im;
}

// code adapted from darknet
int predict_classifier(const Mat& mat)
{
    int top = num_of_class;

    int indexes[num_of_class];
    int size = net.w;

    image im = Mat_to_image(mat);
    image r = resize_min(im, size);
    resize_network(&net, r.w, r.h);

    float *predictions = network_predict(net, im.data);
    if (net.hierarchy)
        hierarchy_predictions(predictions, net.outputs, net.hierarchy, 1, 1);
    top_k(predictions, net.outputs, top, indexes);

    int result = indexes[0];
    
    free_image(im);

    return result;
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "vision_node");
    ros::NodeHandle nh;

    VideoCapture cap;
    VideoWriter output_video;

    parse_options(argc, argv);

    cap.open(video_file);

    if (!cap.isOpened())
    {
        cerr << "cannot open video." << endl;
        return 1;
    }

    if (save_video)
    {
        output_video.open(save_video_file,
                          cap.get(CV_CAP_PROP_FOURCC),
                          cap.get(CV_CAP_PROP_FPS),
                          cv::Size(im_w, im_h));
        if (!output_video.isOpened())
        {
            cerr << "Cannot open video for writing." << endl;
            exit(-1);
        }
        else
        {
            cout << "Saving video to " << save_video_file << endl;
        }
    }

    // set up CNN
    net = parse_network_cfg((char *) cfg_file.c_str());
    load_weights(&net, (char *) weights_file.c_str());
    set_batch_network(&net, 1);
    srand(2222222);

    vector<Rect> raw_bbox, merged_bbox;

    namedWindow("predictions", WINDOW_NORMAL);

    while (ros::ok())
    {
        Mat capture, original, frame;

        if (!cap.read(capture))
            break;

        resize(capture, frame, Size(im_w, im_h), 0, 0, CV_INTER_AREA);

        original = frame.clone();
        raw_bbox.clear();
        merged_bbox.clear();

        for (int x = 0; x < sw_xsteps; ++x)
        {
            for (int y = 0; y < sw_ysteps; ++y)
            {
                Rect grid_rect(x*sw_step_size, 
                               y*sw_step_size,
                               sw_wnd_size,
                               sw_wnd_size);
                Mat grid = original(grid_rect);
                int p = predict_classifier(grid);
                if (p == debris_class) // debris found
                {
                    raw_bbox.push_back(grid_rect);
                }
            }
        }

        merge_bounding_boxes(merged_bbox, raw_bbox, true);
        for (auto bbox : merged_bbox)
            rectangle(frame, bbox, green_colour, 3);

        imshow("predictions", frame);
        if (save_video)
            output_video << frame;
        if(waitKey(1) == 27)
            break;
    }
    cap.release();
    output_video.release();
    return 0;
}

