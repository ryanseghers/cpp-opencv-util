// Copyright(c) 2022 Ryan Seghers
// Distributed under the MIT License (http://opensource.org/licenses/MIT)
#include <string>
#include <exception>
#include <limits>
#include <cmath>
#include <regex>
#include <filesystem>

#include <fmt/core.h>
#include <opencv2/opencv.hpp>
#include <opencv2/core/utils/logger.hpp>

#include "ImageUtil.h"
#include "MiscUtil.h"
#include "StringUtil.h"
#include "MathUtil.h"

using namespace std;
namespace fs = std::filesystem;
using namespace CppBaseUtil;

namespace CppOpenCVUtil
{
    std::string tempDir = "C:/Temp/Images";

    // Function to delete files with the specified pattern from a given directory
    bool delete_matching_files(const std::string& directory, const std::regex& pattern)
    {
        try {
            // Check if the specified path is a directory
            if (fs::is_directory(directory)) {
                // Iterate through the files in the directory
                for (const auto& entry : fs::directory_iterator(directory)) {
                    // Check if the entry is a regular file
                    if (fs::is_regular_file(entry.path())) {
                        // Get the file's name
                        std::string filename = entry.path().filename().string();

                        // Check if the filename matches the specified pattern
                        if (std::regex_match(filename, pattern)) {
                            // Delete the file
                            fs::remove(entry.path());
                        }
                    }
                }
            }
            else {
                std::cerr << "Error: Specified path is not a directory." << std::endl;
                return false;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return false;
        }

        return true;
    }

    std::string saveDebugImage(cv::Mat& img, const char* baseName)
    {
        static int debugImageIndex = 0;

        std::string name = fmt::format("{:03}_{}.tif", debugImageIndex++, baseName);
        std::string path = tempDir + "/" + name;

        cv::imwrite(path, img);

        return path;
    }

    namespace ImageUtil
    {
        void init()
        {
            // clear debug images
            std::regex pattern("^\\d{3}_.+\\.tif$");
            delete_matching_files(tempDir, pattern);

            // turn down verbosity
            cv::utils::logging::setLogLevel(cv::utils::logging::LogLevel::LOG_LEVEL_WARNING);
        }

        // all possible extensions, but all platforms do not support all image types
        static vector<string> allImageExtensions = {
            "jpg", "jpeg", "tif", "tiff", "png", "bmp", "jpe", "ppm", "pgm", "pnm", "ras", "dib", "pxm", "jp2", "webp",
            "exr",                     // no encoder on windows, at least with my build of opencv
            "hdr", "pfm", "sr", "pic", // saving on windows but have not viewed them
            "pbm",                     // my convert and save on windows is apparently not working
        };
        static unordered_map<string, string> allExtensionsToFilterStrings;

        static void initImageExtensions()
        {
            if (allExtensionsToFilterStrings.empty())
            {
                for (string& ext : allImageExtensions)
                {
                    allExtensionsToFilterStrings[ext] = fmt::format("{0}|*.{0}", ext);
                }
            }
        }

        /**
         * @brief Get a map of all extensions (without the period) to file dialog filter strings.
         * Watch out, all platforms do not support all image types.
         */
        unordered_map<string, string> getAllExtensionsToFilterStrings()
        {
            return allExtensionsToFilterStrings;
        }

        /**
         * @brief Get a vector of all extensions (without the period).
         * Watch out, all platforms do not support all image types.
         */
        std::vector<std::string> getAllExtensions()
        {
            return allImageExtensions;
        }

        /**
         * @brief Check if the specified extension is possibly supported by OpenCV load.
         * This doesn't accurately determine the actual support, so some will probably not actually
         * be supported depending on OS and the OpenCV build.
         *
         * I had to hardcode a list so this is probably going to be wrong based on opencv build and certainly
         * wrong the next time it is changed.
         *
         * @param ext Either with or without the period.
         * @return whether or not it is supported
         */
        bool checkSupportedExtension(const std::string& inputExt)
        {
            initImageExtensions();
            string ext = getNormalizedExt(inputExt);
            return std::find(allImageExtensions.begin(), allImageExtensions.end(), ext) != allImageExtensions.end();
        }

        /**
         * @brief Find min and max in image, any type of image but returns floats.
         * @param img
         * @return
         */
        std::pair<float, float> imgMinMax(cv::Mat& img)
        {
            double minVal, maxVal;
            cv::Point2i minLoc, maxLoc;
            cv::minMaxLoc(img, &minVal, &maxVal, &minLoc, &maxLoc);
            float lowVal = (float)minVal;
            float highVal = (float)maxVal;

            if (std::isnan(lowVal) || std::isnan(highVal))
            {
                // happens on mac os when any nan's in the image
                if (img.type() != CV_32F)
                {
                    bail("minmax gave nan on non-32f image");
                }

                // my own
                lowVal = FLT_MAX;
                highVal = -FLT_MAX;

                for (int r = 0; r < img.rows; r++)
                {
                    float* p = img.ptr<float>(r);

                    for (int c = 0; c < img.cols; c++)
                    {
                        float val = p[c];

                        if (!std::isnan(val))
                        {
                            lowVal = std::min(lowVal, val);
                            highVal = std::max(highVal, val);
                        }
                    }
                }

                // handle all nan
                if (highVal < lowVal)
                {
                    lowVal = highVal = NAN;
                }
            }

            return std::pair<float, float>(lowVal, highVal);
        }

        /**
         * @brief Convert to 8u via convertScaleAbs. Computes
         * @param img
         * @param dst
         * @param lowVal Optional. The pixel value in the image to pin to 0 in 8u. Default is to use min and max of image.
         * @param highVal Optional. The pixel value in the image to pin to 255 in 8u. Default is to use min and max of image.
         */
        void imgTo8u(cv::Mat& img, cv::Mat& dst, float lowVal, float highVal)
        {
            if (highVal <= lowVal)
            {
                // range not specified so use min/max
                std::pair<float, float> minMax = imgMinMax(img);
                lowVal = (float)minMax.first;
                highVal = (float)minMax.second;
            }

            // to 8-bit
            float alpha = (float)(255.0 / (float)(highVal - lowVal));
            float beta = -alpha * lowVal;

            cv::convertScaleAbs(img, dst, alpha, beta);
        }

        void imgToRgb(cv::Mat& img8u, uint8_t* dst)
        {
            if (img8u.type() == CV_8U)
            {
                // use cvtColor with cv::Mat wrapper around the dst image
                for (int y = 0; y < img8u.rows; y++)
                {
                    for (int x = 0; x < img8u.cols; x++)
                    {
                        uint8_t val = img8u.at<uint8_t>(y, x);
                        int i = (y * img8u.cols + x) * 3;
                        dst[i] = val;
                        dst[i + 1] = val;
                        dst[i + 2] = val;
                    }
                }
            }
            else
            {
                bail("imgToRgb wrong input image type.");
            }
        }

        std::vector<int> histInt(cv::Mat& img)
        {
            std::vector<int> counts;

            if (img.type() == CV_8U)
            {
                counts.resize(256);

                for (int y = 0; y < img.rows; y++)
                {
                    uint8_t* ps = img.ptr<uint8_t>(y);

                    for (int x = 0; x < img.cols; x++)
                    {
                        counts[ps[x]]++;
                    }
                }
            }
            else if (img.type() == CV_16U)
            {
                counts.resize(65536);

                for (int y = 0; y < img.rows; y++)
                {
                    uint16_t* ps = img.ptr<uint16_t>(y);

                    for (int x = 0; x < img.cols; x++)
                    {
                        counts[ps[x]]++;
                    }
                }
            }
            else
            {
                bail("histInt: Type not handled yet.");
            }

            return counts;
        }

        /**
         * @brief Uniform hist on any type of image but uses float for bins.
         * If maxVal <= minVal then this ignores binCount and returns a single bin (at minVal) with count 0.
         * @param img
         * @param binCount
         * @param minVal Bottom of first bin. If NAN then choose a default. Default is 0.
         * @param maxVal Top of last bin. If NAN then choose a default. Default is max value in image.
         * @param bins
         * @param hist
         */
        void histFloat(cv::Mat& img, int binCount, float& minVal, float& maxVal, vector<float>& bins, vector<int>& hist)
        {
            if (std::isnan(minVal))
            {
                minVal = 0;
            }

            if (std::isnan(maxVal))
            {
                std::pair<float, float> minMax = imgMinMax(img);
                maxVal = minMax.second;
            }

            // no non-nan values in image
            if (std::isnan(maxVal))
            {
                bins.clear();
                hist.clear();
                return;
            }

            if (maxVal <= minVal)
            {
                bins.resize(1);
                bins[0] = minVal;
                hist.resize(1);
                hist[0] = 0;
            }
            else
            {
                // bins
                bins.resize(binCount);
                float binSize = (maxVal - minVal) / binCount;

                for (int i = 0; i < binCount; i++)
                {
                    bins[i] = minVal + i * binSize;
                }

                // upper range val is exclusive, but maxVal may be exactly the max value in image, so increase a little
                // (float epsilon did not work so use a fraction of bin size)
                maxVal += 0.1f * binSize;

                // hist
                float range[] = { minVal, maxVal };
                const float* histRange = { range };
                bool uniform = true;
                bool accumulate = false;

                cv::Mat floatHist;
                cv::Mat mask;
                cv::calcHist(&img, 1, 0, mask, floatHist, 1, &binCount, &histRange, uniform, accumulate);
                floatHist.convertTo(hist, CV_32S);
            }
        }

        void histFloat(cv::Mat& img, int binCount, float minVal, float maxVal, FloatHist& hist)
        {
            hist.minVal = minVal;
            hist.maxVal = maxVal;
            histFloat(img, binCount, hist.minVal, hist.maxVal, hist.bins, hist.counts);
        }

        FloatHist histFloat(cv::Mat& img, int binCount, float minVal, float maxVal)
        {
            FloatHist hist;
            hist.minVal = minVal;
            hist.maxVal = maxVal;
            histFloat(img, binCount, hist.minVal, hist.maxVal, hist.bins, hist.counts);
            return hist;
        }

        /**
         * @brief Compute hist. Bin width is specified by a bit shift for perf.
         * @param img
         * @param binShift bit-shift divisor for how wide bins are
         * @return
         */
        std::vector<int> histInt(cv::Mat& img, int binShift)
        {
            std::vector<int> counts;

            if (img.type() == CV_8U)
            {
                counts.resize(256 >> binShift);

                for (int y = 0; y < img.rows; y++)
                {
                    uint8_t* ps = img.ptr<uint8_t>(y);

                    for (int x = 0; x < img.cols; x++)
                    {
                        counts[ps[x] >> binShift]++;
                    }
                }
            }
            else if (img.type() == CV_16U)
            {
                counts.resize(65536 >> binShift);

                for (int y = 0; y < img.rows; y++)
                {
                    uint16_t* ps = img.ptr<uint16_t>(y);

                    for (int x = 0; x < img.cols; x++)
                    {
                        counts[ps[x] >> binShift]++;
                    }
                }
            }
            else
            {
                bail("histInt: Type not handled yet.");
            }

            return counts;
        }

        /**
         * @brief Compute two percentiles on 8u or 16u image.
         * @param img
         * @param lowPct Percentile to compute, 0 to 100
         * @param highPct Percentile to compute, 0 to 100
         * @return
         */
        std::pair<int, int> histPercentilesInt(cv::Mat& img, float lowPct, float highPct)
        {
            std::vector<int> counts;

            if ((img.type() == CV_8U) || (img.type() == CV_16U))
            {
                counts = histInt(img);
                return std::pair<int, int>(findPercentileInHist(counts, lowPct), findPercentileInHist(counts, highPct));
            }
            else
            {
                bail("histPercentiles: Unsupported image type");
                return std::pair<int, int>(0, 0); // compiler warning
            }
        }

        /**
         * @brief Compute two percentiles on 32f image.
         * @param img
         * @param lowPct Percentile to compute, 0 to 100
         * @param highPct Percentile to compute, 0 to 100
         * @return
         */
        std::pair<float, float> histPercentiles32f(cv::Mat& img, float lowPct, float highPct)
        {
            if (img.type() == CV_32F)
            {
                vector<float> bins;
                vector<int> counts;
                float minVal = NAN;
                float maxVal = NAN;
                histFloat(img, 256, minVal, maxVal, bins, counts);
                int lowIdx = findPercentileInHist(counts, lowPct);
                int highIdx = findPercentileInHist(counts, highPct);
                return std::pair<float, float>(bins[lowIdx], bins[highIdx]);
            }
            else
            {
                bail("histPercentiles32f: Unsupported image type");
                return std::pair<float, float>(NAN, NAN); // compiler warning
            }
        }

        /**
         * @brief Wrapper to handle image types and convert results to pair of float.
         * @param img
         * @param lowPct Percentile to compute, 0 to 100
         * @param highPct Percentile to compute, 0 to 100
         * @return
         */
        std::pair<float, float> histPercentiles(cv::Mat& img, float lowPct, float highPct)
        {
            if ((img.type() == CV_8U) || (img.type() == CV_16U))
            {
                std::pair<int, int> t = histPercentilesInt(img, lowPct, highPct);
                return std::pair<float, float>((float)t.first, (float)t.second);
            }
            else if (img.type() == CV_32F)
            {
                return histPercentiles32f(img, lowPct, highPct);
            }
            else
            {
                bail("histPercentiles: Unsupported image type");
                return std::pair<float, float>(NAN, NAN); // compiler warning
            }
        }

        std::string getImageTypeString(int type)
        {
            if (type == CV_16U)
            {
                return std::string("16U");
            }
            else if (type == CV_8U)
            {
                return std::string("8U");
            }
            else if ((type == CV_32F) || (type == CV_32FC1))
            {
                return std::string("32F");
            }
            else if (type == CV_32FC3)
            {
                return std::string("32FC3");
            }
            else if (type == CV_32S)
            {
                return std::string("32S");
            }
            else if (type == CV_8UC3)
            {
                return std::string("8UC3");
            }
            else if (type == CV_8UC4)
            {
                // or BGRA
                return std::string("ARGB");
            }
            else
            {
                return std::string("UNKNOWN");
            }
        }

        std::string getImageTypeString(cv::Mat& img)
        {
            return getImageTypeString(img.type());
        }

        std::string getImageDescString(cv::Mat& img)
        {
            return fmt::format("{} {}x{}", getImageTypeString(img), img.cols, img.rows);
        }

        /**
         * @brief Get a string representation of the pixel value at the specified location in the image.
         * This returns a string to handle the various image formats, including rgb.
         * @param img
         * @param pt
         * @return
         */
        std::string getPixelValueString(cv::Mat& img, cv::Point2i pt)
        {
            if (!img.empty() && (pt.x >= 0) && (pt.x < img.cols) && (pt.y >= 0) && (pt.y < img.rows))
            {
                int type = img.type();

                if (type == CV_16U)
                {
                    return fmt::format("{}", img.at<uint16_t>(pt.y, pt.x));
                }
                else if (type == CV_8U)
                {
                    return fmt::format("{}", img.at<uint8_t>(pt.y, pt.x));
                }
                else if (type == CV_32S)
                {
                    return fmt::format("{}", img.at<int>(pt.y, pt.x));
                }
                else if (type == CV_32F)
                {
                    return fmt::format("{:.1f}", img.at<float>(pt.y, pt.x));
                }
                else if (type == CV_8UC3)
                {
                    // RGB
                    auto val = img.at<cv::Vec3b>(pt.y, pt.x);
                    return fmt::format("{}, {}, {}", val[0], val[1], val[2]);
                }
                else if (type == CV_8UC4)
                {
                    // ARGB
                    auto val = img.at<cv::Vec4b>(pt.y, pt.x);
                    return fmt::format("{}, {}, {}, {}", val[0], val[1], val[2], val[3]);
                }
                else
                {
                    return fmt::format("OpenCV: {}", type);
                }
            }
            else
            {
                return string();
            }
        }

        /**
         * @brief Compute some stats on the input image.
         * This could be a single-pass function but instead right now uses several cv functions.
         * @param img
         * @return
         */
        ImageStats computeStats(cv::Mat& img)
        {
            ImageStats stats;
            stats.type = img.type();
            stats.width = img.cols;
            stats.height = img.rows;

            // just skip rgb for now, not really handling that case
            if (img.channels() == 1)
            {
                if ((img.type() == CV_8U) || (img.type() == CV_16U))
                {
                    stats.nonzeroCount = cv::countNonZero(img);
                }
                else
                {
                    stats.nonzeroCount = 0;
                }

                if (!img.empty())
                {
                    stats.sum = (float)cv::sum(img)[0];

                    double minVal, maxVal;
                    cv::minMaxLoc(img, &minVal, &maxVal);
                    stats.minVal = (float)minVal;
                    stats.maxVal = (float)maxVal;
                }
                else
                {
                    stats.sum = 0;
                    stats.minVal = NAN;
                    stats.maxVal = NAN;
                }
            }

            return stats;
        }

        /**
         * @brief Convert input image to save in the specified output file, since not all combinations
         * are supported.
         * This is barely started, probably many more could be done.
         *
         * @param img
         * @param inputExt File extension, with or without the period.
         * @param dst Output image. This is only set if a conversion is done.
         * @return True if any conversion was done.
         */
        bool convertAfterLoad(cv::Mat& img, const std::string& inputExt, cv::Mat& dst)
        {
            bool isChanged = false;
            string ext = getNormalizedExt(inputExt);

            // TIF coming in swapped
            if ((ext == "tif") || (ext == "tiff"))
            {
                if (img.type() == CV_8UC3)
                {
                    cv::cvtColor(img, dst, cv::COLOR_BGR2RGB);
                    isChanged = true;
                }
                else if (img.type() == CV_8UC4)
                {
                    cv::cvtColor(img, dst, cv::COLOR_BGRA2BGR);
                    isChanged = true;
                }
            }

            return isChanged;
        }

        /**
         * @brief Convert input image to save in the specified output file, since not all combinations
         * are supported.
         * This is barely started, probably many more could be done.
         *
         * @param img
         * @param inputExt File extension, with or without the period.
         * @param dst Output image. This gets set regardless of whether any conversion is done. If no conversion
         *     then this is just set (operator=) to refer to the input image.
         * @return True if any conversion was done.
         */
        bool convertForSave(cv::Mat& img, const std::string& inputExt, cv::Mat& dst)
        {
            bool isChanged = false;
            string ext = getNormalizedExt(inputExt);
            bool isTiff = (ext == "tif") || (ext == "tiff");
            int type = img.type();
            cv::Mat tmp;

            // for 16U, 32F, 32S to non-tiff, auto-range to 8u
            if (((type == CV_16U) || (type == CV_32S) || (type == CV_32F)) && !isTiff)
            {
                std::pair<float, float> t = ImageUtil::histPercentiles(img, 1.0f, 99.0f);
                ImageUtil::imgTo8u(img, dst, t.first, t.second);
                isChanged = true;
            }
            // for 32S to tiff, convert to 32F
            else if ((type == CV_32S) && isTiff)
            {
                img.convertTo(dst, CV_32F);
                isChanged = true;
            }
            else if (ext == "ppm")
            {
                // ppm needs BGR
                if ((type == CV_8U) || (type == CV_16U) || (type == CV_32S) || (type == CV_32F))
                {
                    cv::cvtColor(img, dst, cv::COLOR_GRAY2BGR);
                    isChanged = true;
                }
                else if (type == CV_8UC3)
                {
                    // no-op
                    dst = img;
                    isChanged = false;
                }
                else if (type == CV_8UC4)
                {
                    cv::cvtColor(img, dst, cv::COLOR_BGRA2BGR, CV_8UC1);
                    isChanged = true;
                }
                else
                {
                    throw std::runtime_error("Unhandled input image type for ppm output.");
                }
            }
            else if ((ext == "pbm") || (ext == "pgm"))
            {
                // pbm needs 8UC1
                // pgm just says "gray" but use 8UC1 also for that
                if ((type == CV_8U) || (type == CV_16U) || (type == CV_32S) || (type == CV_32F))
                {
                    img.convertTo(dst, CV_8UC1);
                    isChanged = true;
                }
                else if (type == CV_8UC3)
                {
                    cv::cvtColor(img, dst, cv::COLOR_BGR2GRAY, CV_8UC1);
                    isChanged = true;
                }
                else if (type == CV_8UC4)
                {
                    cv::cvtColor(img, dst, cv::COLOR_BGRA2GRAY, CV_8UC1);
                    isChanged = true;
                }
                else
                {
                    throw std::runtime_error("Unhandled input image type for pbm output.");
                }
            }
            else
            {
                dst = img;
                isChanged = false;
            }

            return isChanged;
        }

        /**
         * @brief Create a 32F gaussian kernel image.
         * Mostly by ChatGPT-4.
         * @param ksize
         * @param sigma
         * @return
         */
        cv::Mat generateGaussianKernel(int ksize, float sigma)
        {
            // Ensure kernel size is odd
            if (ksize % 2 == 0)
            {
                std::cerr << "Kernel size must be odd." << std::endl;
                return cv::Mat();
            }

            // Create 1D Gaussian kernel
            cv::Mat gaussian1D = cv::getGaussianKernel(ksize, sigma, CV_32F);

            // Compute the 2D Gaussian kernel by multiplying the 1D kernels
            cv::Mat gaussian2D = gaussian1D * gaussian1D.t();

            return gaussian2D;
        }

        /**
         * @brief Add a small image (kernel) to another image at a specified integer location.
         * Partially by ChatGPT-4.
         * @param image Image to add kernel to.
         * @param kernel 32F kernel to add
         */
        void addKernelToImage(cv::Mat& image, const cv::Mat& kernel, int x, int y)
        {
            // Loop through the kernel
            for (int j = 0; j < kernel.rows; ++j)
            {
                for (int i = 0; i < kernel.cols; ++i)
                {
                    // Add kernel value to the corresponding image pixel (if within image bounds)
                    if (x + i >= 0 && x + i < image.cols && y + j >= 0 && y + j < image.rows)
                    {
                        if (image.type() == CV_32F)
                        {
                            image.at<float>(y + j, x + i) += static_cast<float>(kernel.at<float>(j, i));
                        }
                        else if (image.type() == CV_8UC1)
                        {
                            image.at<uint8_t>(y + j, x + i) += static_cast<uint8_t>(kernel.at<float>(j, i));
                        }
                        else
                        {
                            throw std::runtime_error("The specified image type is not implemented.");
                        }
                    }
                }
            }
        }

        /**
         * @brief Render the list of images into a single output image (dst) in a grid of rows and colums, per the spec.
         * Partially by ChatGPT-4.
         * @param images The images to render. They must all be 8UC1 or 8UC3. These will all be rendered to the aspect ratio of the first image.
         * @param captions A caption for each image (empty vector or strings for no caption).
         * @param spec Parameters for how to render.
         * @param dst Output image.
         */
        void renderCollage(const std::vector<cv::Mat>& images, const std::vector<std::string>& captions, const CollageSpec& spec, cv::Mat& dst)
        {
            int imgCount = (int)images.size();

            if (imgCount == 0)
            {
                return;
            }

            // setup for text
            cv::Scalar captionColor = spec.doBlackBackground ? cv::Scalar(255, 255, 255) : cv::Scalar(0, 0, 0);
            int baseline;
            int exampleTextHeight = cv::getTextSize(std::string("Foo1"), spec.fontFace, spec.fontScale, 1, &baseline).height;
            int captionMargin = exampleTextHeight / 2;

            if (!spec.doCaptions)
            {
                exampleTextHeight = 0;
                captionMargin = 0;
            }

            // assume all images same aspect ratio
            int imgRows = images[0].rows;
            int imgCols = images[0].cols;

            // compute dims
            int fullWidth = spec.imageWidthPx;
            int totalMarginCol = (spec.colCount + 1) * spec.marginPx;
            int subImgWidth = (fullWidth - totalMarginCol) / spec.colCount; // width of each small image
            int rowCount = (imgCount + spec.colCount - 1) / spec.colCount;  // number of rows of images
            double imgScale = ((double)spec.imageWidthPx - totalMarginCol) / (spec.colCount * imgCols);
            int subImgHeight = (int)(imgScale * imgRows);
            int totalTextHeight = 2 * captionMargin + exampleTextHeight;
            int fullHeight = subImgHeight * rowCount + spec.marginPx * (rowCount + 1) + rowCount * totalTextHeight;

            // create image
            dst.create(fullHeight, fullWidth, CV_8UC3);
            dst.setTo(spec.doBlackBackground ? cv::Scalar(0, 0, 0) : cv::Scalar(255, 255, 255));

            // render images and captions
            for (int i = 0; i < imgCount; i++)
            {
                int row = i / spec.colCount;
                int col = i % spec.colCount;
                cv::Mat imgScaled;
                cv::resize(images[i], imgScaled, cv::Size(subImgWidth, subImgHeight));

                int x = col * imgScaled.cols + (col + 1) * spec.marginPx;
                int y = row * imgScaled.rows + (row + 1) * spec.marginPx + row * totalTextHeight;
                cv::Rect roi(x, y, imgScaled.cols, imgScaled.rows);

                if (imgScaled.type() == CV_8UC1)
                {
                    cv::cvtColor(imgScaled, imgScaled, cv::COLOR_GRAY2RGB);
                }

                imgScaled.copyTo(dst(roi));

                if (spec.doCaptions && !captions.empty() && !captions[i].empty())
                {
                    std::string s = captions[i];
                    cv::Size textSize = cv::getTextSize(s, spec.fontFace, spec.fontScale, 1, &baseline);

                    // clip string to fit (this is not efficient but I'm not sure how else to do it)
                    while (textSize.width >= subImgWidth)
                    {
                        s = s.substr(0, s.size() - 1);
                        textSize = cv::getTextSize(s, spec.fontFace, spec.fontScale, 1, &baseline);
                    }

                    cv::Point textOrg(x + (imgScaled.cols - textSize.width) / 2, y + imgScaled.rows + textSize.height + captionMargin);
                    cv::putText(dst, s, textOrg, spec.fontFace, spec.fontScale, captionColor, 1, cv::LINE_AA);
                }
            }
        }

        /**
         * @brief Create a profile (row or col sums) on input image.
         * @param img
         * @param doVert
         * @param profile Values are put in by reserve() and push_back().
         */
        void profile(cv::Mat& img, bool doVert, std::vector<float>& profile)
        {
            int n = doVert ? img.cols : img.rows;

            // reduce (and convert to float)
            cv::Mat mf;
            cv::reduce(img, mf, doVert ? 0 : 1, cv::REDUCE_SUM, CV_32F);

            // put in vector
            profile.reserve(n);

            for (int i = 0; i < n; i++)
            {
                if (doVert)
                {
                    profile.push_back(mf.at<float>(0, i));
                }
                else
                {
                    profile.push_back(mf.at<float>(i, 0));
                }
            }
        }

        /**
         * @brief Choose a color (black or white) to maximize contrast vs original pixel color at the specified point. By ChatGPT.
         */
        cv::Scalar computeTextColor(cv::Mat& img, cv::Point pixel)
        {
            // Get the color of the given pixel
            cv::Vec3b pixel_color = img.at<cv::Vec3b>(pixel);

            // Convert the pixel color to grayscale
            cv::Mat gray;
            cv::cvtColor(cv::Mat(1, 1, CV_8UC3, pixel_color), gray, cv::COLOR_BGR2GRAY);

            // Compute the luminance of the pixel color
            double pixel_luminance = gray.at<uchar>(0, 0) / 255.0;

            // Compute the contrast ratio with black and white text
            double contrast_with_black = (pixel_luminance + 0.05) / 0.05;
            double contrast_with_white = (1.05 - pixel_luminance) / 0.05;

            // Choose the text color that maximizes the contrast
            cv::Scalar text_color;

            if (contrast_with_black > contrast_with_white)
            {
                text_color = cv::Scalar(0, 0, 0);
            }
            else
            {
                text_color = cv::Scalar(255, 255, 255);
            }

            return text_color;
        }

        bool ensureMat(cv::Mat& mat, int nRows, int nCols, int type)
        {
            if (mat.rows != nRows || mat.cols != nCols || mat.type() != type)
            {
                mat.create(nRows, nCols, type);
                return true;
            }
            else
            {
                return false;
            }
        }

        void printMatInfo(cv::Mat& mat)
        {
            fmt::print("rows: {}\n", mat.rows);
            fmt::print("cols: {}\n", mat.cols);
            fmt::print("channels: {}\n", mat.channels());
            fmt::print("type: {} {}\n", mat.type(), ImageUtil::getImageTypeString(mat));
            fmt::print("elemSize: {}\n", mat.elemSize());
            fmt::print("step[0]: {} (bytes, step to next row)\n", mat.step[0]);
            fmt::print("step[1]: {} (bytes, step to next col)\n", mat.step[1]);
            fmt::print("step1: {} (not bytes, not elements, but values e.g. single float for 3-channel float image)\n", mat.step1());
            fmt::print("calculated stride: {} (elements)\n", mat.step[0] / mat.elemSize());
            fmt::print("isContinuous: {}\n", mat.isContinuous());
        }

        void zeroOutsideRoi(cv::Mat& mat, const cv::Rect& roi)
        {
            CV_Assert(mat.type() == CV_32F);
            CV_Assert(roi.x >= 0 && roi.y >= 0 && roi.x + roi.width <= mat.cols && roi.y + roi.height <= mat.rows);

            // Create a mask with the same size as the input mat, initialized with zeros
            cv::Mat mask(mat.size(), CV_8U, cv::Scalar(0));

            // Set the ROI region in the mask to white (255)
            mask(roi) = 255;

            //// Create a copy of the input mat
            //cv::Mat result = mat.clone();

            // Set the elements outside the ROI to zero using the mask
            mat.setTo(0, ~mask);

            //// Copy the result back to the input mat
            //result.copyTo(mat);
        }
    }
}
