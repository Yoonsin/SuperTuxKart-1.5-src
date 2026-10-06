import csv

def parse_stk_replay(replay_filepath, csv_filepath):
    extracted_data = []

    # 1. 파일 읽기 시작
    with open(replay_filepath, 'r', encoding='utf-8') as file:
        for line in file:
            # 양쪽 공백이나 줄바꿈 문자 제거
            line = line.strip()
            
            # 빈 줄은 건너뜀
            if not line:
                continue
            
            # 띄어쓰기를 기준으로 데이터를 리스트로 쪼갬
            parts = line.split()
            
            # 2. 데이터 필터링 로직
            # 레이스 데이터 줄은 좌표와 쿼터니언 회전값 등 20개 이상의 항목을 가짐
            # 헤더 정보(version, track 등)를 무시하기 위해 항목이 10개 이상인 줄만 취급
            if len(parts) > 10:
                try:
                    # 첫 번째 값이 실수(float)인 시간 데이터인지 검증
                    time_val = float(parts[0])
                    x_val = float(parts[1])
                    y_val = float(parts[2])
                    z_val = float(parts[3])
                    
                    # 검증을 통과하면 [Time, X, Y, Z] 형태로 저장 (원본 문자열 그대로 유지)
                    extracted_data.append([parts[0], parts[1], parts[2], parts[3]])
                except ValueError:
                    # "size: 1556" 같은 헤더 줄에서 변환 에러가 나면 그냥 건너뜀
                    continue

    # 3. CSV(엑셀) 파일로 내보내기
    with open(csv_filepath, 'w', newline='', encoding='utf-8') as csvfile:
        writer = csv.writer(csvfile)
        # 첫 번째 줄에 엑셀 헤더(열 이름) 작성
        writer.writerow(["Time", "X", "Y", "Z"])
        # 추출한 모든 데이터 한 번에 쓰기
        writer.writerows(extracted_data)
        
    print(f"✅ 추출 완료! 총 {len(extracted_data)}개의 프레임 좌표가 '{csv_filepath}' 파일로 저장되었습니다.")

# ====== 실행 부분 ======
if __name__ == "__main__":
    # 사용 방법: ("원본 리플레이 파일 이름", "만들고 싶은 엑셀 파일 이름")
    # 원본 파일이 스크립트와 같은 폴더에 있어야 합니다.
    parse_stk_replay("abyss_2025120_1_100_463.replay", "world_record_path.csv")
