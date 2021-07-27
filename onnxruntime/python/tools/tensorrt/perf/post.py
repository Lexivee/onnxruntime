import argparse
import mysql.connector
import sys
import os
import pandas as pd
from sqlalchemy import create_engine

# database connection strings 
sql_connector = 'mysql+mysqlconnector://'
user='ort@onnxruntimedashboard'
password=os.environ.get('DASHBOARD_MYSQL_ORT_PASSWORD')
host='onnxruntimedashboard.mysql.database.azure.com'
database='onnxruntime'

# table names
fail = 'fail'
memory = 'memory'
latency = 'latency'
status = 'status'
latency_over_time = 'latency_over_time'
        
def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-r", "--report_folder", help="Path to the local file report", required=True)
    parser.add_argument(
        "-c", "--commit_hash", help="Commit id", required=True)
    parser.add_argument(
        "-u", "--report_url", help="Report Url", required=True)
    parser.add_argument(
        "-t", "--trt_version", help="Tensorrt Version", required=True)
    return parser.parse_args()

def parse_csv(report_file):
    table = pd.read_csv(report_file)
    return table

def get_latency_over_time(commit_hash, report_url, latency_table):
    if not latency_table.empty:
        to_drop = ['TrtGain_CudaFp32', 'EpGain_TrtFp32', 'TrtGain_CudaFp16', 'EpGain_TrtFp16']
        over_time = latency_table.drop(to_drop, axis='columns')
        over_time = over_time.melt(id_vars=['Model', 'Group'], var_name='Ep', value_name='Latency')
            
        import time   
        datetime = time.strftime('%Y-%m-%d %H:%M:%S')
        over_time = over_time.assign(UploadTime=datetime)
        over_time = over_time.assign(CommitId=commit_hash)
        over_time = over_time.assign(ReportUrl=report_url)
            
        over_time = over_time[['UploadTime', 'CommitId', 'Model', 'Ep', 'Latency', 'ReportUrl', 'Group']]
        over_time.rename(columns={"Group":"ModelGroup"}, inplace=True)
        over_time.fillna('', inplace=True)
        return over_time
    
def adjust_columns(table, columns, db_columns, model_group): 
    table = table[columns]
    table = table.set_axis(db_columns, axis=1)
    table = table.assign(Group=model_group)
    return table 

def get_failures(fail, model_group):
    fail_columns = fail.keys()
    fail_db_columns = ['Model', 'Ep', 'ErrorType', 'ErrorMessage']
    fail = adjust_columns(fail, fail_columns, fail_db_columns, model_group)
    return fail

def get_memory(memory, model_group): 
    memory_columns = ['Model', \
                      'CUDA EP fp32 \npeak memory usage (MiB)', \
                      'TRT EP fp32 \npeak memory usage (MiB)', \
                      'Standalone TRT fp32 \npeak memory usage (MiB)', \
                      'CUDA EP fp16 \npeak memory usage (MiB)', \
                      'TRT EP fp16 \npeak memory usage (MiB)', \
                      'Standalone TRT fp16 \npeak memory usage (MiB)' \
                      ]
    memory_db_columns = ['Model', 'CudaFp32', 'TrtFp32', 'StandaloneFp32', 'CudaFp16', 'TrtFp16', 'StandaloneFp16']
    memory = adjust_columns(memory, memory_columns, memory_db_columns, model_group)
    return memory

def get_latency(latency, model_group):
    latency_columns = ['Model', \
                        'CPU fp32 \nmean (ms)', \
                        'CUDA fp32 \nmean (ms)', \
                        'TRT EP fp32 \nmean (ms)', \
                        'Standalone TRT fp32 \nmean (ms)', \
                        'TRT v CUDA EP fp32 \ngain (mean) (%)', \
                        'EP v Standalone TRT fp32 \ngain (mean) (%)',     
                        'CUDA fp16 \nmean (ms)', \
                        'TRT EP fp16 \nmean (ms)', \
                        'Standalone TRT fp16 \nmean (ms)', \
                        'TRT v CUDA EP fp16 \ngain (mean) (%)', \
                        'EP v Standalone TRT fp16 \ngain (mean) (%)' \
                        ]
    latency_db_columns = ['Model', 'CpuFp32', 'CudaEpFp32', 'TrtEpFp32', 'StandaloneFp32', 'TrtGain_CudaFp32', 'EpGain_TrtFp32', \
                        'CudaEpFp16', 'TrtEpFp16', 'StandaloneFp16', 'TrtGain_CudaFp16', 'EpGain_-TrtFp16']
    latency = adjust_columns(latency, latency_columns, latency_db_columns, model_group)
    return latency
    
def get_status(status, model_group):
    status_columns = status.keys()
    status_db_columns = ['Model', 'CpuFp32', 'CudaEpFp32', 'TrtEpFp32', 'StandaloneFp32', 'CudaEpFp16', 'TrtEpFp16', 'StandaloneFp16']
    status = adjust_columns(status, status_columns, status_db_columns, model_group)
    return status

def write_table(engine, table, table_name, trt_version, drop_duplicates=True):
    if table.empty:
        return
    table = table.assign(TrtVersion=trt_version) # add TrtVersion
    table.to_sql(table_name, con=engine, if_exists='append', index=False, chunksize=1)
    if drop_duplicates: 
        full_table = pd.read_sql("SELECT * FROM " + table_name,  engine)
        full_table.drop_duplicates(keep='last', inplace=True)
        table.to_sql(table_name, con=engine, if_exists='replace', index=False, chunksize=1)

def write_latency_over_time(engine, table, table_name, trt_version):

    # delete using cursor for large table
    conn = engine.raw_connection()
    cursor = conn.cursor()
    delete_query = ('DELETE FROM onnxruntime.ep_model_latency_over_time '
                    'WHERE UploadTime < DATE_SUB(Now(), INTERVAL 100 DAY);'
                    )

    cursor.execute(delete_query)
    conn.commit()
    cursor.close()
    conn.close()

    # write table 
    write_table(engine, table, table_name, trt_version, drop_duplicates=False)
    
def main():
    
    # connect to database
    args = parse_arguments()
    connection_string = sql_connector + \
                        user + ':' + \
                        password + \
                        '@' + host + '/' + \
                        database
    engine = create_engine(connection_string)

    try:
        result_file = args.report_folder

        folders = os.listdir(result_file)
        os.chdir(result_file)

        tables = [fail, memory, latency, status, latency_over_time]
        table_results = {}
        for table_name in tables:
            table_results[table_name] = pd.DataFrame()

        for model_group in folders: 
            os.chdir(model_group)
            csv_filenames = os.listdir()
            for csv in csv_filenames:
                table = parse_csv(csv)
                if fail in csv:
                    table_results[fail] = table_results[fail].append(get_failures(table, model_group), ignore_index=True)
                if latency in csv:
                    table_results[memory] = table_results[memory].append(get_memory(table, model_group), ignore_index=True)
                    table_results[latency] = table_results[latency].append(get_latency(table, model_group), ignore_index=True)
                    table_results[latency_over_time] = table_results[latency_over_time].append(get_latency_over_time(args.commit_hash, args.report_url, table_results[latency]), ignore_index=True)
                if status in csv: 
                    table_results[status] = table_results[status].append(get_status(table, model_group), ignore_index=True)
            os.chdir(result_file)

        for table in tables: 
            print('writing ' + table + ' over time to database')
            db_table_name = 'ep_model_' + table
            if table == 'latency_over_time':
                write_latency_over_time(engine, table_results[table], db_table_name, args.trt_version)
            else:
                write_table(engine, table_results[table], db_table_name, args.trt_version)

    except BaseException as e: 
        print(str(e))
        sys.exit(1)

if __name__ == "__main__":
    main()
