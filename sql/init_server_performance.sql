-- init_server_performance.sql
-- 创建数据库
CREATE DATABASE IF NOT EXISTS monitor_db 
CHARACTER SET utf8mb4 
COLLATE utf8mb4_unicode_ci;

USE monitor_db;

-- 主机信息表
CREATE TABLE IF NOT EXISTS host_info (
    host_id VARCHAR(50) PRIMARY KEY,
    host_name VARCHAR(100) NOT NULL,
    ip_address VARCHAR(45),
    num_cpus INT DEFAULT 0,
    total_memory_gb FLOAT,
    total_disk_gb FLOAT,
    os_version VARCHAR(100),
    kernel_version VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_host_name (host_name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- CPU监控数据表
CREATE TABLE IF NOT EXISTS cpu_metrics (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    host_id VARCHAR(50) NOT NULL,
    timestamp DATETIME NOT NULL,
    cpu_name VARCHAR(20) DEFAULT 'all',
    cpu_percent FLOAT,
    usr_percent FLOAT,
    system_percent FLOAT,
    nice_percent FLOAT,
    idle_percent FLOAT,
    io_wait_percent FLOAT,
    irq_percent FLOAT,
    soft_irq_percent FLOAT,
    load_avg_1 FLOAT,
    load_avg_3 FLOAT,
    load_avg_15 FLOAT,
    INDEX idx_host_time (host_id, timestamp),
    INDEX idx_timestamp (timestamp),
    INDEX idx_cpu_name (cpu_name),
    FOREIGN KEY (host_id) REFERENCES host_info(host_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 软中断统计表
CREATE TABLE IF NOT EXISTS soft_irq_metrics (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    host_id VARCHAR(50) NOT NULL,
    timestamp DATETIME NOT NULL,
    cpu VARCHAR(20),
    hi FLOAT,
    timer FLOAT,
    net_tx FLOAT,
    net_rx FLOAT,
    block FLOAT,
    irq_poll FLOAT,
    tasklet FLOAT,
    sched FLOAT,
    hrtimer FLOAT,
    rcu FLOAT,
    INDEX idx_host_cpu_time (host_id, cpu, timestamp),
    INDEX idx_timestamp (timestamp),
    FOREIGN KEY (host_id) REFERENCES host_info(host_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 内存监控数据表
CREATE TABLE IF NOT EXISTS memory_metrics (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    host_id VARCHAR(50) NOT NULL,
    timestamp DATETIME NOT NULL,
    total_gb FLOAT,
    free_gb FLOAT,
    available_gb FLOAT,
    buffers_gb FLOAT,
    cached_gb FLOAT,
    swap_cached_gb FLOAT,
    active_gb FLOAT,
    inactive_gb FLOAT,
    dirty_gb FLOAT,
    writeback_gb FLOAT,
    anon_pages_gb FLOAT,
    mapped_gb FLOAT,
    used_percent FLOAT,
    INDEX idx_host_time (host_id, timestamp),
    INDEX idx_timestamp (timestamp),
    FOREIGN KEY (host_id) REFERENCES host_info(host_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 磁盘监控数据表 - 修正版本
CREATE TABLE IF NOT EXISTS disk_metrics (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    host_id VARCHAR(50) NOT NULL,
    timestamp DATETIME NOT NULL,
    disk_name VARCHAR(50),
    `reads` BIGINT,
    `writes` BIGINT,
    sectors_read BIGINT,
    sectors_written BIGINT,
    read_time_ms BIGINT,
    write_time_ms BIGINT,
    io_in_progress BIGINT,
    io_time_ms BIGINT,
    weighted_io_time_ms BIGINT,
    read_mbps FLOAT,
    write_mbps FLOAT,
    read_iops FLOAT,
    write_iops FLOAT,
    avg_read_latency_ms FLOAT,
    avg_write_latency_ms FLOAT,
    util_percent FLOAT,
    INDEX idx_host_disk_time (host_id, disk_name, timestamp),
    INDEX idx_timestamp (timestamp),
    INDEX idx_util (util_percent),
    FOREIGN KEY (host_id) REFERENCES host_info(host_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 网络监控数据表
CREATE TABLE IF NOT EXISTS network_metrics (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    host_id VARCHAR(50) NOT NULL,
    timestamp DATETIME NOT NULL,
    interface_name VARCHAR(50),
    send_rate_mbps FLOAT,
    rcv_rate_mbps FLOAT,
    send_packets_pps FLOAT,
    rcv_packets_pps FLOAT,
    INDEX idx_host_interface_time (host_id, interface_name, timestamp),
    INDEX idx_timestamp (timestamp),
    FOREIGN KEY (host_id) REFERENCES host_info(host_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 性能聚合表
CREATE TABLE IF NOT EXISTS performance_summary (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    host_id VARCHAR(50) NOT NULL,
    time_bucket DATETIME NOT NULL,
    bucket_type ENUM('hourly', 'daily', 'weekly') NOT NULL,
    avg_cpu_percent FLOAT,
    max_cpu_percent FLOAT,
    avg_load_1 FLOAT,
    max_load_1 FLOAT,
    avg_memory_usage FLOAT,
    max_memory_usage FLOAT,
    min_available_memory_gb FLOAT,
    max_disk_util FLOAT,
    avg_disk_iops FLOAT,
    peak_disk_throughput_mbps FLOAT,
    avg_network_throughput_mbps FLOAT,
    peak_network_throughput_mbps FLOAT,
    performance_score FLOAT,
    INDEX idx_host_bucket (host_id, bucket_type, time_bucket),
    UNIQUE KEY uniq_host_time_bucket (host_id, bucket_type, time_bucket),
    FOREIGN KEY (host_id) REFERENCES host_info(host_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 创建视图：当前性能状态
CREATE OR REPLACE VIEW current_performance_status AS
SELECT 
    h.host_name,
    MAX(c.timestamp) as last_update,
    AVG(c.cpu_percent) as avg_cpu_last_5min,
    MAX(c.load_avg_1) as current_load,
    m.used_percent as memory_usage,
    MAX(d.util_percent) as max_disk_usage,
    MAX(n.total_throughput_mbps) as max_network_throughput
FROM host_info h
LEFT JOIN cpu_metrics c ON h.host_id = c.host_id 
    AND c.timestamp >= NOW() - INTERVAL 5 MINUTE
LEFT JOIN memory_metrics m ON h.host_id = m.host_id 
    AND m.timestamp >= NOW() - INTERVAL 5 MINUTE
LEFT JOIN disk_metrics d ON h.host_id = d.host_id 
    AND d.timestamp >= NOW() - INTERVAL 5 MINUTE
LEFT JOIN (
    SELECT host_id, MAX(send_rate_mbps + rcv_rate_mbps) as total_throughput_mbps
    FROM network_metrics 
    WHERE timestamp >= NOW() - INTERVAL 5 MINUTE
    GROUP BY host_id
) n ON h.host_id = n.host_id
GROUP BY h.host_id, h.host_name;

-- 创建用户并授权（可选，根据需要调整）
-- CREATE USER IF NOT EXISTS 'monitor_user'@'localhost' IDENTIFIED BY 'password123';
-- GRANT ALL PRIVILEGES ON monitor_db.* TO 'monitor_user'@'localhost';
-- FLUSH PRIVILEGES;

-- 创建清理旧数据的存储过程
DELIMITER $$

CREATE PROCEDURE cleanup_old_metrics(IN days_to_keep INT)
BEGIN
    DECLARE delete_cutoff DATETIME;
    SET delete_cutoff = DATE_SUB(NOW(), INTERVAL days_to_keep DAY);
    
    -- 删除详细指标数据
    DELETE FROM cpu_metrics WHERE timestamp < delete_cutoff;
    DELETE FROM soft_irq_metrics WHERE timestamp < delete_cutoff;
    DELETE FROM memory_metrics WHERE timestamp < delete_cutoff;
    DELETE FROM disk_metrics WHERE timestamp < delete_cutoff;
    DELETE FROM network_metrics WHERE timestamp < delete_cutoff;
    
    -- 保留每日汇总数据30天，小时汇总数据7天
    DELETE FROM performance_summary 
    WHERE bucket_type = 'hourly' 
    AND time_bucket < DATE_SUB(NOW(), INTERVAL 7 DAY);
    
    DELETE FROM performance_summary 
    WHERE bucket_type = 'daily' 
    AND time_bucket < DATE_SUB(NOW(), INTERVAL 30 DAY);
    
    SELECT CONCAT('Cleaned up data older than ', days_to_keep, ' days') as message;
END$$

DELIMITER ;

-- 创建事件，每天凌晨3点清理90天前的数据
CREATE EVENT IF NOT EXISTS daily_cleanup
ON SCHEDULE EVERY 1 DAY
STARTS CONCAT(CURDATE() + INTERVAL 1 DAY, ' 03:00:00')
DO
CALL cleanup_old_metrics(90);

-- 启用事件调度器
SET GLOBAL event_scheduler = ON;

-- 初始化完成消息
SELECT 'Database initialization completed successfully!' as message;

-- 显示表结构
SHOW TABLES;