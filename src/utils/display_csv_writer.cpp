#include "acceltool/utils/display_csv_writer.h"

#include <stdexcept>

namespace acceltool
{
    DisplayCsvWriter::~DisplayCsvWriter()
    {
        flush();
    }

    void DisplayCsvWriter::open(const std::string& path)
    {
        m_out.open(path, std::ios::out | std::ios::trunc);
        if (!m_out.is_open())
        {
            throw std::runtime_error("Failed to open display CSV file: " + path);
        }
    }

    void DisplayCsvWriter::writeHeader()
    {
        m_out << "bucket_index,"
                 "start_sample_index,"
                 "end_sample_index,"
                 "start_device_timestamp_unix_ns,"
                 "end_device_timestamp_unix_ns,"
                 "sample_count,"
                 "peak_x,"
                 "peak_y,"
                 "peak_z,"
                 "max_magnitude_xy,"
                 "max_magnitude_xyz,"
                 "max_norm_Lat_G\n";
    }

    void DisplayCsvWriter::writeRow(const DisplayBucket& bucket)
    {
        m_out
            << bucket.bucketIndex << ','
            << bucket.startSampleIndex << ','
            << bucket.endSampleIndex << ','
            << bucket.startDeviceTimestampUnixNs << ','
            << bucket.endDeviceTimestampUnixNs << ','
            << bucket.sampleCount << ','
            << bucket.peakX << ','
            << bucket.peakY << ','
            << bucket.peakZ << ','
            << bucket.maxMagnitudeXY << ','
            << bucket.maxMagnitudeXYZ << ','
            << bucket.maxNormLatG << '\n';
    }


    void DisplayCsvWriter::flush()
    {
        if (m_out.is_open())
        {
            m_out.flush();
        }
    }

    bool DisplayCsvWriter::isOpen() const noexcept
    {
        return m_out.is_open();
    }
}
