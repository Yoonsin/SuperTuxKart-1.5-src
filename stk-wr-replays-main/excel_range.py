
import csv
import numpy as np
def load_csv_data(filepath):
    """CSV 파일에서 데이터를 읽어와 시간 리스트와 좌표 배열로 반환합니다."""
    times, coords = [], []
    with open(filepath, 'r', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for row in reader:
            times.append(row['Time'])
            coords.append([float(row['X']), float(row['Y']), float(row['Z'])])
    return np.array(times), np.array(coords)

def match_closest_points(wr_csv_path, player_csv_path, output_csv_path):
    # 1. WR과 Player 데이터 로드
    wr_times, wr_coords = load_csv_data(wr_csv_path)
    p_times, p_coords = load_csv_data(player_csv_path)
    
    combined_data = []
    
    # 트랙이 교차하는 구간에서 엉뚱한 바퀴(Lap)의 데이터를 잡지 않도록 탐색 범위 제한
    search_window = 500
    closest_p_idx = 0
    
    print(f"🔄 총 {len(wr_coords)}개의 월드 레코드 좌표를 기준으로 매칭을 시작합니다...")
    
    # 2. WR의 모든 좌표를 하나씩 순회하며 가장 가까운 플레이어 좌표 찾기
    for i, wr_pos in enumerate(wr_coords):
        wr_time = wr_times[i]
        
        # 플레이어 데이터 탐색 범위 설정 (이전에 찾았던 근접점의 앞뒤 500프레임)
        start_idx = max(0, closest_p_idx - search_window)
        end_idx = min(len(p_coords), closest_p_idx + search_window)
        sub_p_coords = p_coords[start_idx:end_idx]
        
        # WR 좌표와 플레이어 탐색 범위 내 모든 좌표 간의 X, Y, Z 3차원 거리 계산
        diffs = sub_p_coords - wr_pos
        distances = np.sqrt(np.sum(diffs**2, axis=1))
        
        # 가장 거리가 짧은(가까운) 플레이어의 인덱스 추출
        local_min_idx = np.argmin(distances)
        closest_p_idx = start_idx + local_min_idx
        
        # 추출한 인덱스의 플레이어 시간, 좌표, 그리고 두 점 사이의 실제 거리
        p_time = p_times[closest_p_idx]
        p_x, p_y, p_z = p_coords[closest_p_idx]
        distance = distances[local_min_idx]
        
        # 3. 엑셀에 기록할 한 줄(Row) 구성 (요청하신 빈 칸 포함)
        row = [
            wr_time, wr_pos[0], wr_pos[1], wr_pos[2],  # WR 데이터 (왼쪽)
            p_time, p_x, p_y, p_z,                     # Player 데이터 (오른쪽)
            round(distance, 4)                         # 참고용: 두 점 사이의 오차 거리
        ]
        combined_data.append(row)
        
    # 4. 새로운 CSV 파일로 내보내기
    with open(output_csv_path, 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        # 맨 윗줄에 열 이름(Header) 작성
        writer.writerow([
            "WR_Time", "WR_X", "WR_Y", "WR_Z", 
            "Player_Time", "Player_X", "Player_Y", "Player_Z", 
            "XYZ_Distance"
        ])
        writer.writerows(combined_data)
        
    print(f"✅ 매칭 완료! 결과가 '{output_csv_path}' 파일로 저장되었습니다.")

# ====== 실행 부분 ======
if __name__ == "__main__":
    # 파일 이름 지정 ("WR 파일", "플레이어 파일", "출력될 엑셀 파일 이름")
    match_closest_points("world_record_path_hacienda.csv", "standard_record_path_hacienda.csv"
                         , "hacienda(1)_comparison.csv")
