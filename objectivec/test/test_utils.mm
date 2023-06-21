// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#import "test_utils.h"

NS_ASSUME_NONNULL_BEGIN

namespace testUtils {

NSString* createTemporaryDirectory(XCTestCase* testCase) {
  NSString* temporaryDirectory = NSTemporaryDirectory();
  NSString* directoryPath = [temporaryDirectory stringByAppendingPathComponent:@"ort-objective-c-test"];

  NSError* error = nil;
  [[NSFileManager defaultManager] createDirectoryAtPath:directoryPath
                            withIntermediateDirectories:YES
                                             attributes:nil
                                                  error:&error];
  if (error) {
    NSLog(@"Error creating temporary directory: %@", error.localizedDescription);
    return nil;
  }

  // add teardown block to delete the temporary directory
  [testCase addTeardownBlock:^{
    NSError* error = nil;
    [[NSFileManager defaultManager] removeItemAtPath:directoryPath error:&error];
    if (error) {
      NSLog(@"Error deleting temporary directory: %@", error.localizedDescription);
    }
  }];

  return directoryPath;
}

NSArray<NSNumber*>* getFloatArrayFromData(NSData* data) {
  NSMutableArray<NSNumber*>* array = [NSMutableArray array];
  float value;
  for (size_t i = 0; i < data.length / sizeof(float); ++i) {
    [data getBytes:&value range:NSMakeRange(i * sizeof(float), sizeof(float))];
    [array addObject:[NSNumber numberWithFloat:value]];
  }
  return array;
}

}  // namespace testUtils

NS_ASSUME_NONNULL_END
